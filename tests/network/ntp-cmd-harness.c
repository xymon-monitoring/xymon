/* SPDX-License-Identifier: GPL-2.0-or-later                                  */
/*
 * tests/network/ntp-cmd-harness.c
 *
 * Regression test for the xymonnet "ntp" test tool selection and command
 * construction (xymonnet/ntpcmd.c). PR #158 review found that the sntp
 * invocation passed '-t', which classic ntp.org sntp 4.2.8 rejects (it spells
 * the unicast-response timeout '-u' and has no '-t'); the RRD-parser harness
 * could not catch it because it only exercises output parsing, not the command
 * line. This drives the REAL selection/build code (the file is #included the
 * same way xymond/do_rrd.c #includes its parsers) so the per-tool timeout flag
 * and the priority/fallback rules are pinned.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stdarg.h>

/* ntpcmd.c warns about unknown NTPTOOL entries via errprintf - stub it. */
static int errcount = 0;
static void errprintf(const char *fmt, ...)
{
	va_list ap;
	errcount++;
	va_start(ap, fmt);
	vfprintf(stderr, "  (errprintf) ", ap);	/* prefix only; quiet on pass */
	vfprintf(stderr, fmt, ap);
	va_end(ap);
}

#include "../../xymonnet/ntpcmd.c"

/* ---- test driver ---------------------------------------------------------- */

static int failures = 0;

static void check_str(const char *what, const char *got, const char *want)
{
	if (strcmp(got, want) != 0) {
		failures++;
		fprintf(stderr, "FAIL %s\n      got:  %s\n      want: %s\n", what, got, want);
	}
	else {
		printf("ok   %s = %s\n", what, got);
	}
}

static void check_tool(const char *what, enum ntptool_t got, enum ntptool_t want)
{
	if (got != want) {
		failures++;
		fprintf(stderr, "FAIL %s: got %d want %d\n", what, got, want);
	}
	else {
		printf("ok   %s -> %s\n", what, ntp_toolname[want]);
	}
}

/* Availability callback driven by a bitmask of installed tools (bit idx). */
static int avail_mask(int idx, void *ud)
{
	return (*(unsigned *)ud >> idx) & 1u;
}

#define BIT(t) (1u << (t))

int main(void)
{
	char buf[2048];

	/* --- command construction: the per-tool timeout flag is the crux --- */
	ntp_build_cmd(buf, sizeof(buf), NTP_NTPDATE, "/usr/bin/ntpdate", "-u -q -p 1", "10.0.0.1", 9);
	check_str("ntpdate cmd", buf, "/usr/bin/ntpdate -u -q -p 1 10.0.0.1 2>&1");

	ntp_build_cmd(buf, sizeof(buf), NTP_NTPDIG, "/usr/bin/ntpdig", "", "10.0.0.1", 9);
	check_str("ntpdig cmd (-t)", buf, "/usr/bin/ntpdig  -t 9 10.0.0.1 2>&1");

	/* The regression: ntp.org sntp must get -u, never -t. */
	ntp_build_cmd(buf, sizeof(buf), NTP_SNTP, "/usr/bin/sntp", "", "10.0.0.1", 9);
	check_str("sntp cmd (-u not -t)", buf, "/usr/bin/sntp  -u 9 10.0.0.1 2>&1");
	if (strstr(buf, " -t ")) { failures++; fprintf(stderr, "FAIL sntp must not pass -t\n"); }

	ntp_build_cmd(buf, sizeof(buf), NTP_CHRONYD, "/usr/sbin/chronyd", "", "10.0.0.1", 9);
	check_str("chronyd cmd", buf, "/usr/sbin/chronyd -Q  -t 9 'server 10.0.0.1 iburst' 2>&1");

	/* --- tool selection: priority, forcing, fallback, legacy SNTP --- */
	{
		unsigned all = BIT(NTP_NTPDATE)|BIT(NTP_NTPDIG)|BIT(NTP_SNTP)|BIT(NTP_CHRONYD);
		unsigned only_chrony = BIT(NTP_CHRONYD);
		unsigned none = 0;

		/* Default order ntpdig|ntpdate|sntp|chronyd: ntpdig wins when present */
		check_tool("default, all installed", ntp_select_tool(NULL, 0, avail_mask, &all), NTP_NTPDIG);

		/* Default order, only chronyd installed: skip down to chronyd */
		check_tool("default, only chronyd", ntp_select_tool(NULL, 0, avail_mask, &only_chrony), NTP_CHRONYD);

		/* Nothing installed: fall back to first valid choice (ntpdig), not NONE */
		check_tool("default, none installed", ntp_select_tool(NULL, 0, avail_mask, &none), NTP_NTPDIG);

		/* A single name forces that tool even when others are installed */
		check_tool("force sntp", ntp_select_tool("sntp", 0, avail_mask, &all), NTP_SNTP);

		/* Forced tool not installed: still selected (error surfaces at run time) */
		check_tool("force sntp, absent", ntp_select_tool("sntp", 0, avail_mask, &none), NTP_SNTP);

		/* Legacy SNTP=... (NTPTOOL unset) forces sntp */
		check_tool("legacy SNTP set", ntp_select_tool(NULL, 1, avail_mask, &none), NTP_SNTP);

		/* Explicit NTPTOOL wins over the legacy SNTP flag */
		check_tool("NTPTOOL over legacy SNTP", ntp_select_tool("ntpdate", 1, avail_mask, &all), NTP_NTPDATE);

		/* Unknown entries are warned about and skipped, valid ones still honoured */
		errcount = 0;
		check_tool("unknown then chronyd", ntp_select_tool("bogus|chronyd", 0, avail_mask, &only_chrony), NTP_CHRONYD);
		if (errcount != 1) { failures++; fprintf(stderr, "FAIL expected 1 warning for unknown entry, got %d\n", errcount); }

		/* Case-insensitive matching */
		check_tool("case-insensitive", ntp_select_tool("SNTP", 0, avail_mask, &all), NTP_SNTP);
	}

	if (failures) {
		fprintf(stderr, "\n%d assertion(s) failed\n", failures);
		return 1;
	}
	printf("\nall NTP tool selection / command construction assertions passed\n");
	return 0;
}
