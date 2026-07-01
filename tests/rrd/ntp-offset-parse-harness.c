/* SPDX-License-Identifier: GPL-2.0-or-later                                  */
/*
 * tests/rrd/ntp-offset-parse-harness.c
 *
 * Drives the REAL offset recording path for the "ntp" test - do_net.c's parser
 * and do_ntpstat.c - the same way xymond/do_rrd.c #includes them, with the RRD
 * plumbing stubbed so the values that WOULD be written to ntp.rrd are captured.
 *
 * Pins, across all three backends, that the clock offset is parsed and stored in
 * milliseconds (seconds x 1000):
 *   - the built-in SNTP probe banner  ("... offset +0.001234 +/- 0.005678 sec ...")
 *   - ntpdate                          ("... offset -0.040324, delay ...")
 *   - sntp                             ("... + 0.038766 +/- 0.052900 secs")
 * plus that the probe's "+/-" root distance is recorded as the 2nd DS ("U" when a
 * backend omits it), and that do_ntpstat parses "offset=" even at the very start
 * of the message (the #183 out-of-bounds-read case) without scaling its ms value.
 *
 * Built under AddressSanitizer/UBSan by the runner so the pointer arithmetic in
 * the parsers is bounds-checked.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

/* --- stub the RRD plumbing the parsers call (real versions live in do_rrd.c) --- */
char rrdvalues[8192];
static char captured[8192];	/* rrdvalues last handed to create_and_update_rrd() */
static int  rrd_writes;

static void  setupfn(char *f, char *p) { (void)f; (void)p; }
static void  setupfn2(char *f, char *a, char *b) { (void)f; (void)a; (void)b; }
static void  setupfn3(char *f, char *a, char *b, char *c) { (void)f; (void)a; (void)b; (void)c; }
static void *setup_template(char *p[]) { (void)p; return (void *)1; }
static int   create_and_update_rrd(char *h, char *t, char *c, char *pp, char *cp[], void *tpl)
{ (void)h; (void)t; (void)c; (void)pp; (void)cp; (void)tpl; snprintf(captured, sizeof(captured), "%s", rrdvalues); rrd_writes++; return 0; }
static char *xgetenv(const char *n) { (void)n; return ""; }
static char *xstrdup(const char *s) { return strdup(s); }
#define xfree free

#include "../../xymond/rrd/do_ntpstat.c"
#include "../../xymond/rrd/do_net.c"

static int fails;

/* Run an "ntp"-test message through do_net_rrd and return the recorded ms value
 * (or NO_RECORD if nothing was written). */
#define NO_RECORD (-1e9)
static double record_ntp(const char *msg)
{
	char buf[1024];
	double ms; int ts;

	snprintf(buf, sizeof(buf), "%s", msg);
	captured[0] = '\0'; rrd_writes = 0;
	do_net_rrd("host", "ntp", "", "", buf, (time_t)1000);
	if (!rrd_writes || sscanf(captured, "%d:%lf", &ts, &ms) != 2) return NO_RECORD;
	return ms;
}

static void check(const char *name, double got, double want)
{
	double d = got - want; if (d < 0) d = -d;
	if (d > 0.0005) { printf("FAIL %s: got %.6f, want %.6f\n", name, got, want); fails++; }
	else            { printf("ok   %s: %.3f ms\n", name, got); }
}

int main(void)
{
	/* offset reported in seconds by every backend -> recorded in ms (x1000) */
	check("probe banner",
	      record_ntp("NTP server 1.2.3.4 is synchronised\n\nstratum 2, offset +0.001234 +/- 0.005678 sec, delay 0.000900 sec\n"),
	      1.234);
	check("ntpdate",
	      record_ntp("server 1.2.3.4, stratum 3, offset -0.040324, delay 0.02568\n"),
	      -40.324);
	/* sntp: "<date> <time> <sign> <offset> +/- <err> secs" (the parser reads the
	 * date/time as the first two whitespace tokens and requires "secs" as the last,
	 * so an ISO date stays one token and there is no trailing newline). */
	check("sntp",
	      record_ntp("2009-11-13 11:29:10.000313 + 0.038766 +/- 0.052900 secs"),
	      38.766);
	check("no offset -> nothing recorded",
	      record_ntp("server 1.2.3.4 is unreachable\n") == NO_RECORD ? 0.0 : 1.0, 0.0);
	/* A non-numeric "offset" token must be skipped, not silently recorded as 0:
	 * the parser validates the token with strtod()'s end pointer. */
	check("non-numeric offset -> nothing recorded",
	      record_ntp("server 1.2.3.4, stratum 3, offset unavailable, delay 0.0\n") == NO_RECORD ? 0.0 : 1.0, 0.0);
	/* A numeric prefix followed by junk is not a valid token either - only the
	 * trailing comma ntpdate appends is allowed after the number. */
	check("partial-numeric offset -> nothing recorded",
	      record_ntp("server 1.2.3.4, stratum 3, offset 12junk, delay 0.0\n") == NO_RECORD ? 0.0 : 1.0, 0.0);
	/* strtod() parses "nan"/"inf" as valid doubles; isfinite() must reject them
	 * so a non-finite value never reaches ntp.rrd. */
	check("non-finite offset -> nothing recorded",
	      record_ntp("server 1.2.3.4, stratum 3, offset nan, delay 0.0\n") == NO_RECORD ? 0.0 : 1.0, 0.0);

	/* The built-in probe also records the "+/-" root distance as the 2nd DS, in ms.
	 * ntpdate has no "+/-", so its root distance is recorded unknown ("U"). */
	captured[0] = '\0'; rrd_writes = 0;
	{
		char buf[1024]; double off, rd; int ts;
		snprintf(buf, sizeof(buf), "%s",
			 "NTP server 1.2.3.4 is synchronised\n\nstratum 2, offset +0.001234 +/- 0.005678 sec, delay 0.000900 sec\n");
		do_net_rrd("host", "ntp", "", "", buf, (time_t)1000);
		if (rrd_writes && sscanf(captured, "%d:%lf:%lf", &ts, &off, &rd) == 3)
			check("probe root distance", rd, 5.678);
		else { printf("FAIL probe root distance: captured='%s'\n", captured); fails++; }
	}
	captured[0] = '\0'; rrd_writes = 0;
	{
		char buf[1024];
		snprintf(buf, sizeof(buf), "%s", "server 1.2.3.4, stratum 3, offset -0.040324, delay 0.02568\n");
		do_net_rrd("host", "ntp", "", "", buf, (time_t)1000);
		if (rrd_writes && strstr(captured, ":U")) printf("ok   ntpdate root distance = U (absent)\n");
		else { printf("FAIL ntpdate root distance not U: captured='%s'\n", captured); fails++; }
	}

	/* do_ntpstat parses "offset=" at the very start of msg (the #183 OOB case) and
	 * stores its value as-is (already ms; no x1000 here). */
	rrdvalues[0] = '\0'; captured[0] = '\0'; rrd_writes = 0;
	do_ntpstat_rrd("host", "ntpstat", "", "", "offset=12.500000\n", (time_t)1000);
	{
		double ms = NO_RECORD; int ts;
		if (rrd_writes) sscanf(captured, "%d:%lf", &ts, &ms);
		check("do_ntpstat offset= at start (#183)", ms, 12.5);
	}

	if (fails) { printf("%d check(s) FAILED\n", fails); return 1; }
	printf("all offset-parse checks ok\n");
	return 0;
}
