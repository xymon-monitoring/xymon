#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/notbefore-notafter.sh
#
# Regression guard for issue #298: the NOTBEFORE:/NOTAFTER: hosts.cfg tags did
# not work. timestr2timet() converts their YYYYMMDDHHMM timestamps and had two
# defects, both fixed together:
#
#   1. struct tm was never initialized. tm_sec was left holding whatever was on
#      the stack, and mktime() normalizes it, so the converted time moved by an
#      unbounded amount -- "202001011200" came out as 1995-12-20 17:57:17 in one
#      measurement and 2046-09-03 in another. A host carrying NOTBEFORE: was
#      therefore either permanently visible or permanently invisible, depending
#      on stack residue.
#
#   2. It truncated the string in place with *(s+N) = '\0'. lib/loadhosts.c
#      hands it the pointer xmh_item() returned, which points into the host
#      record's own tag buffer, so the stored tag was left as "NOTBEFORE:2020" --
#      the same aliasing that caused the noflap bug in #276. Everything that
#      reports raw tags showed the mutilated value: XMH_RAW, hostinfo responses,
#      the info page's "Other tags:" row, xymongrep.
#
# The window assertions below exercise defect 1 and the record assertion
# exercises defect 2. Only the record assertion fails deterministically against
# the old code -- the window ones depend on what was on the stack -- so that is
# the one that anchors this test.
#
# Dates are deliberately far outside any plausible run time, so the test does
# not rot.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)

[ -f "$ROOT/lib/timefunc.c" ] || skip "lib/timefunc.c not present in this checkout"

CC=${CC:-cc}
command -v "$CC" >/dev/null 2>&1 || skip "no C compiler available (CC=$CC)"

[ -f "$ROOT/include/config.h" ] && [ -f "$ROOT/lib/libxymoncomm.a" ] \
	|| skip "tree not built (run make first; the post-build CI suite covers this)"

work=$(mktemp -d "${TMPDIR:-/tmp}/xymon-notbefore.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM

make -C "$ROOT/lib" libxymoncomm.a >"$work/libbuild.log" 2>&1 \
	|| { cat "$work/libbuild.log" >&2; fail "cannot refresh libxymoncomm.a"; }

cat > "$work/hosts.cfg" <<'EOF'
127.0.0.1 inwindow.example.com # conn NOTBEFORE:199001010000 NOTAFTER:209001010000
127.0.0.1 notyet.example.com # conn NOTBEFORE:209001010000
127.0.0.1 expired.example.com # conn NOTAFTER:199001010000
127.0.0.1 plain.example.com # conn
EOF

cat > "$work/harness.c" <<'EOF'
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "libxymon.h"

static int failures = 0;

static void expect(const char *label, int cond, const char *detail)
{
	if (!cond) {
		fprintf(stderr, "%s: %s\n", label, detail);
		failures++;
	}
}

int main(int argc, char *argv[])
{
	void *h;
	char *raw;
	time_t t;
	struct tm *lt;
	char stamp[] = "202001011200";	/* writable: the old code wrote into it */

	if (argc != 2) { fprintf(stderr, "usage: %s hosts.cfg\n", argv[0]); return 2; }

	if (load_hostnames(argv[1], NULL, 1) == -1) {
		fprintf(stderr, "cannot load %s\n", argv[1]);
		return 2;
	}

	/* The conversion itself must be exact, and must not depend on what was on
	 * the stack when it ran. */
	t = timestr2timet(stamp);
	lt = localtime(&t);
	expect("timestr2timet converts the timestamp it was given",
	       lt && (lt->tm_year + 1900 == 2020) && (lt->tm_mon == 0) && (lt->tm_mday == 1) &&
	       (lt->tm_hour == 12) && (lt->tm_min == 0) && (lt->tm_sec == 0),
	       "202001011200 must convert to 2020-01-01 12:00:00 - every field mktime() "
	       "reads has to be set, an uninitialized tm_sec moves the result by years");

	/* And it must leave its argument alone: callers pass a pointer into the
	 * host record's own tag buffer. */
	expect("timestr2timet does not write into the string it was given",
	       strcmp(stamp, "202001011200") == 0,
	       "the caller's string must come back unchanged - lib/loadhosts.c passes "
	       "the pointer xmh_item() returned, which aliases the stored tag");

	/* hosts.cfg(5) documents the timestamp as exactly 12 digits, so anything
	 * else has to be refused rather than converted into a date nobody wrote.
	 * Neither of these is caught by sscanf() on its own: "%d" skips leading
	 * whitespace and accepts a sign, and mktime() normalizes a date that does
	 * not exist instead of failing. */
	{
		char signed_[] = "-20200101120";	/* 12 chars, one of them a sign */
		char spaced[]  = "2020 1011200";	/* 12 chars, one of them a space */
		char month13[] = "202013011200";	/* mktime() would say 2021-01-01 */
		char feb31[]   = "202002311200";	/* mktime() would say 2020-03-02 */
		char leapok[]  = "202002291200";	/* 2020 is a leap year: must pass */

		expect("a timestamp with a sign is refused", timestr2timet(signed_) == -1,
		       "'-20200101120' is not twelve digits");
		expect("a timestamp with a space is refused", timestr2timet(spaced) == -1,
		       "'2020 1011200' is not twelve digits");
		expect("a month that does not exist is refused", timestr2timet(month13) == -1,
		       "month 13 must not be normalized into January of the next year");
		expect("a day that does not exist is refused", timestr2timet(feb31) == -1,
		       "31 February must not be normalized into 2 March");
		expect("a real leap day is accepted", timestr2timet(leapok) != -1,
		       "29 February 2020 exists and must convert");
	}

	/* The window itself. */
	expect("a host inside its window is visible",
	       hostinfo("inwindow.example.com") != NULL,
	       "NOTBEFORE in the past and NOTAFTER in the future must leave the host active");
	expect("a host before its NOTBEFORE is hidden",
	       hostinfo("notyet.example.com") == NULL,
	       "a host whose NOTBEFORE has not arrived must be treated as not listed");
	expect("a host after its NOTAFTER is hidden",
	       hostinfo("expired.example.com") == NULL,
	       "a host whose NOTAFTER has passed must be treated as not listed");
	expect("control: a host with neither tag is visible",
	       hostinfo("plain.example.com") != NULL,
	       "an untagged host must be unaffected, so the assertions above cannot be "
	       "satisfied by hiding everything");

	/* The record, which is what defect 2 destroyed. */
	h = hostinfo("inwindow.example.com");
	raw = (h ? xmh_item(h, XMH_RAW) : NULL);
	expect("loading a host leaves its NOTBEFORE/NOTAFTER tags intact",
	       raw && strstr(raw, "NOTBEFORE:199001010000") && strstr(raw, "NOTAFTER:209001010000"),
	       raw ? raw : "(no record)");

	printf(failures ? "FAILED\n" : "ALL OK\n");
	return failures ? 1 : 0;
}
EOF

# Link flags come from the build's own Makefile: SSLLIBS is absent on a
# --no-ssl build, and carries the -L a non-standard OpenSSL prefix needs.
ssllibs=$(sed -n 's/^SSLLIBS *= *//p' "$ROOT/Makefile")

"$CC" -iquote "$ROOT/include" -iquote "$ROOT/lib" -o "$work/harness" \
	"$work/harness.c" "$ROOT/lib/libxymoncomm.a" \
	$ssllibs 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; fail "harness does not compile"; }

"$work/harness" "$work/hosts.cfg" 2>"$work/stderr.log" \
	|| fail "NOTBEFORE/NOTAFTER assertions failed:
$(cat "$work/stderr.log")"

pass "NOTBEFORE:/NOTAFTER: convert exactly and leave the host record intact"
