/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * tests/rrd/ntp-rrd-handoff-harness.c
 *
 * Regression test for the network "ntp" test -> ntpstat RRD chain.
 *
 * do_net_rrd() parses the NTP tool output, builds an "offset=<ms>"
 * message and hands it to do_ntpstat_rrd(). The message starts with
 * the "offset=" token, so do_ntpstat_rrd() must accept a match at the
 * very beginning of the message: before this was fixed it always read
 * *(p-1) - one byte before the buffer - and (usually) rejected the
 * value, so the handoff silently produced no RRD update.
 *
 * The real xymond/rrd/do_ntpstat.c and do_net.c are #included below in
 * the same order as xymond/do_rrd.c does it, against stub RRD plumbing
 * that records each update instead of touching an RRD file. Compile
 * with -fsanitize=address (the driver script does, when available) and
 * any reintroduced out-of-bounds read becomes a hard crash even if the
 * stray byte happens to satisfy the comparison.
 *
 * Exit code 0 = all cases pass, 1 = at least one failure.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <time.h>
#include <limits.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

/* ---- stub plumbing, mirroring xymond/do_rrd.c ---------------------------- */

static char rrdvalues[4096];
static char rrdfn[PATH_MAX];

#define MAXUPDATES 8
static struct {
	char fn[PATH_MAX];
	char values[4096];
} updates[MAXUPDATES];
static int update_count = 0;

static void *setup_template(char *params[])
{
	(void)params;
	return (void *)1;	/* non-NULL, so the handlers set it up only once */
}

static void setupfn(char *format, char *param)
{
	snprintf(rrdfn, sizeof(rrdfn), format, param);
}

static void setupfn2(char *format, char *param1, char *param2)
{
	snprintf(rrdfn, sizeof(rrdfn), format, param1, param2);
}

static void setupfn3(char *format, char *param1, char *param2, char *param3)
{
	snprintf(rrdfn, sizeof(rrdfn), format, param1, param2, param3);
}

static int create_and_update_rrd(char *hostname, char *testname, char *classname, char *pagepaths, char *creparams[], void *template)
{
	(void)hostname; (void)testname; (void)classname; (void)pagepaths; (void)creparams; (void)template;

	if (update_count < MAXUPDATES) {
		snprintf(updates[update_count].fn, sizeof(updates[update_count].fn), "%s", rrdfn);
		snprintf(updates[update_count].values, sizeof(updates[update_count].values), "%s", rrdvalues);
	}
	update_count++;
	return 0;
}

static char *xgetenv(const char *name)
{
	char *v = getenv(name);

	if (v) return v;
	if (strcmp(name, "PINGCOLUMN") == 0) return "conn";
	return "";
}

#define xstrdup strdup
#define xfree free

/* Same inclusion order as xymond/do_rrd.c */
#include "../../xymond/rrd/do_ntpstat.c"
#include "../../xymond/rrd/do_net.c"

/* ---- test driver ---------------------------------------------------------- */

static int failures = 0;
static const time_t tstamp = 1234567890;

static void reset(void)
{
	update_count = 0;
	rrdfn[0] = rrdvalues[0] = '\0';
}

/* Find the recorded update for an RRD file, or NULL */
static const char *find_update(const char *fn)
{
	int i, n = (update_count < MAXUPDATES) ? update_count : MAXUPDATES;

	for (i = 0; i < n; i++) {
		if (strcmp(updates[i].fn, fn) == 0) return updates[i].values;
	}
	return NULL;
}

static void expect_ntpstat(const char *casename, double want_ms)
{
	const char *values = find_update("ntpstat.rrd");
	float got;

	if (!values) {
		printf("FAIL %s: no ntpstat.rrd update recorded (%d updates total)\n", casename, update_count);
		failures++;
	}
	else if (sscanf(values, "%*d:%f", &got) != 1) {
		printf("FAIL %s: unparseable rrdvalues '%s'\n", casename, values);
		failures++;
	}
	else if (fabs(got - want_ms) > 0.0005) {
		printf("FAIL %s: expected offsetms %.6f, got %.6f\n", casename, want_ms, got);
		failures++;
	}
	else {
		printf("ok   %s (offsetms=%.6f)\n", casename, got);
	}
}

static void expect_no_ntpstat(const char *casename)
{
	if (find_update("ntpstat.rrd") != NULL) {
		printf("FAIL %s: unexpected ntpstat.rrd update '%s'\n", casename, find_update("ntpstat.rrd"));
		failures++;
	}
	else {
		printf("ok   %s (no update, as expected)\n", casename);
	}
}

/* Run one tool output through the full network-test chain */
static void netcase(const char *casename, const char *tooloutput, double want_ms)
{
	char msg[4096];

	snprintf(msg, sizeof(msg), "green Thu Jun 11 22:28:28 2026 ntp ok\n\n%s\n", tooloutput);
	reset();
	do_net_rrd("testhost", "ntp", "ntp", "", msg, tstamp);
	expect_ntpstat(casename, want_ms);
}

int main(void)
{
	char raw[32];
	char msg[4096];

	/* The five tool output formats documented in do_net.c, through the
	 * full do_net_rrd() -> do_ntpstat_rrd() chain. All offsets convert
	 * from seconds to the milliseconds stored in the ntpstat RRD. */
	netcase("ntpdate classic",
		"server 172.16.10.2, stratum 3, offset -0.040324, delay 0.02568\n"
		"13 Nov 11:29:06 ntpdate[7038]: adjust time server 172.16.10.2 offset -0.040324 sec",
		-40.324);
	netcase("sntp 4.2.6 detached + sign",
		"2009 Nov 13 11:29:10.000313 + 0.038766 +/- 0.052900 secs",
		38.766);
	netcase("sntp 4.2.6 detached - sign",
		"2009 Nov 13 11:29:10.000313 - 0.038766 +/- 0.052900 secs",
		-38.766);
	netcase("ntpdig / sntp 4.2.7+",
		"2015-10-14 13:46:04.534916 (+0500) -0.000007 +/- 0.084075 localhost 127.0.0.1 s2 no-leap",
		-0.007);
	netcase("macOS sntp",
		"+0.009083 +/- 0.013184 pool.ntp.org 193.134.29.12",
		9.083);
	netcase("chronyd -Q",
		"2026-06-11T22:28:28Z System clock wrong by 0.368564 seconds (ignored)",
		368.564);

	/* The ntpsec ntpdate wrapper prints the ntpdig format; the word
	 * "ntpdate" in the message must not derail the format detection. */
	netcase("ntpsec ntpdate wrapper (ntpdig format)",
		"ntpdate: i/o timeout workaround enabled\n"
		"2026-06-11 22:28:28.123456 (+0000) +0.001500 +/- 0.002000 ntp.example.com 192.0.2.1 s2 no-leap",
		1.5);

	/* Failure output must not produce an update */
	reset();
	snprintf(msg, sizeof(msg), "red Thu Jun 11 22:28:28 2026 ntp NOT ok\n\nno server suitable for synchronization\n");
	do_net_rrd("testhost", "ntp", "ntp", "", msg, tstamp);
	expect_no_ntpstat("unreachable server");

	/* do_ntpstat_rrd() directly: the handoff message starts with the
	 * "offset=" token. raw[0] makes the byte before the message a known
	 * non-space, non-comma value, so the pre-fix *(p-1) check fails
	 * deterministically instead of depending on stack garbage. */
	raw[0] = 'X';
	strcpy(raw+1, "offset=-3.500000");
	reset();
	do_ntpstat_rrd("testhost", "ntp", "ntp", "", raw+1, tstamp);
	expect_ntpstat("offset= at start of message (do_net.c handoff)", -3.5);

	/* The existing client-side feeds must keep working */
	reset();
	do_ntpstat_rrd("testhost", "ntpstat", "ntpstat", "",
		"associd=0 status=0615, ... rootdisp=10.230, offset=1.234, frequency=-9.985 ...", tstamp);
	expect_ntpstat("ntpq -c rv output (comma before token)", 1.234);

	reset();
	do_ntpstat_rrd("testhost", "ntpstat", "ntpstat", "",
		"status\nOffset: 2.500000\n", tstamp);
	expect_ntpstat("legacy LARRD BF script output", 2.5);

	/* ...and the token must still not match mid-word */
	reset();
	do_ntpstat_rrd("testhost", "ntpstat", "ntpstat", "", "rawoffset=9.9", tstamp);
	expect_no_ntpstat("offset= preceded by non-separator");

	if (failures) {
		printf("%d case(s) FAILED\n", failures);
		return 1;
	}
	printf("all cases passed\n");
	return 0;
}
