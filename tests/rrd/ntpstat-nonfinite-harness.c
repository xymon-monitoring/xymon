/* SPDX-License-Identifier: GPL-2.0-or-later                                  */
/*
 * tests/rrd/ntpstat-nonfinite-harness.c
 *
 * Pins the finite-value guard in do_ntpstat_rrd() (xymond/rrd/do_ntpstat.c).
 * The "ntpq -c rv" / LARRD offset is read with sscanf("%f"), which happily
 * parses "nan"/"inf"; without a guard, "%f" then writes "nan"/"inf" into the
 * RRD update string, which RRDtool rejects (or stores as NaN, poisoning the
 * graph). The handler must drop a non-finite sample and record nothing.
 *
 * Drives the REAL do_ntpstat.c against stub RRD plumbing (the same shape as
 * xymond/do_rrd.c uses), with no rrdtool and no socket. Built with
 * AddressSanitizer when the toolchain supports it.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
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
	return (void *)1;	/* non-NULL: the handler sets it up only once */
}

static void setupfn(char *format, char *param)
{
	snprintf(rrdfn, sizeof(rrdfn), format, param);
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

#include "../../xymond/rrd/do_ntpstat.c"

/* ---- test driver ---------------------------------------------------------- */

static int failures = 0;
static const time_t tstamp = 1234567890;

static void reset(void)
{
	update_count = 0;
	rrdfn[0] = rrdvalues[0] = '\0';
}

static const char *find_update(const char *fn)
{
	int i, n = (update_count < MAXUPDATES) ? update_count : MAXUPDATES;

	for (i = 0; i < n; i++)
		if (strcmp(updates[i].fn, fn) == 0) return updates[i].values;
	return NULL;
}

static void expect_offset(const char *casename, double want_ms)
{
	const char *values = find_update("ntpstat.rrd");
	float got;

	if (!values) {
		printf("FAIL %s: no ntpstat.rrd update recorded\n", casename); failures++;
	}
	else if (sscanf(values, "%*d:%f", &got) != 1) {
		printf("FAIL %s: unparseable rrdvalues '%s'\n", casename, values); failures++;
	}
	else if (fabs(got - want_ms) > 0.0005) {
		printf("FAIL %s: expected offsetms %.6f, got %.6f\n", casename, want_ms, got); failures++;
	}
	else {
		printf("ok   %s (offsetms=%.6f)\n", casename, got);
	}
}

/* A non-finite sample must record NOTHING - and the recorded string must never
 * contain "nan"/"inf" even if an update somehow slipped through. */
static void expect_dropped(const char *casename)
{
	const char *values = find_update("ntpstat.rrd");

	if (values != NULL) {
		printf("FAIL %s: recorded an update for a non-finite offset: '%s'\n", casename, values);
		failures++;
	}
	else {
		printf("ok   %s (no update, as expected)\n", casename);
	}
}

static void run(const char *msg)
{
	reset();
	do_ntpstat_rrd("testhost", "ntpstat", "ntpstat", "", (char *)msg, tstamp);
}

int main(void)
{
	/* Sanity: a normal finite offset is still recorded (ntpq -c rv form, and the
	 * legacy LARRD "Offset:" form). ntpq offset is already in milliseconds. */
	run("associd=0 status=0615, rootdisp=10.230, offset=1.234, frequency=-9.985");
	expect_offset("finite ntpq offset recorded", 1.234);

	run("status\nOffset: 2.500000\n");
	expect_offset("finite LARRD offset recorded", 2.5);

	/* The guard: non-finite values must be dropped, not forwarded to RRDtool. */
	run("associd=0 status=0615, offset=nan, frequency=-9.985");
	expect_dropped("ntpq offset=nan dropped");

	run("associd=0 status=0615, offset=inf, frequency=-9.985");
	expect_dropped("ntpq offset=inf dropped");

	run("associd=0 status=0615, offset=-inf, frequency=-9.985");
	expect_dropped("ntpq offset=-inf dropped");

	run("status\nOffset: nan\n");
	expect_dropped("LARRD Offset: nan dropped");

	if (failures) {
		fprintf(stderr, "\n%d check(s) failed\n", failures);
		return 1;
	}
	printf("\nall do_ntpstat non-finite-offset checks passed\n");
	return 0;
}
