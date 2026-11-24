#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/matchregex-error.sh
#
# matchregex() (lib/matching.c) reports a PCRE2 match *error* as "no match",
# just like a real no-match, so a rule keeps working -- but it must log the
# error, or a pattern that errors on every evaluation (an invalid UTF-8 byte
# under a (*UTF) pattern, a match-limit blowup) silently never matches and a
# missed alert leaves no trace.
#
# A failing rule fails on every evaluation, and analysis.cfg's LOG rules are
# evaluated once per line of each client's log excerpt, so the log line is
# limited: one per pattern per minute. And it names the pattern where the
# caller has its text, as patternmatch() and namematch() do.
#
# Checked through a harness that links the real library: a (*UTF) pattern
# against an invalid UTF-8 subject makes pcre2_match() return a UTF error
# (< -1, not PCRE2_ERROR_NOMATCH). Each of matchregex(), patternmatch() and
# namematch() is called 10000 times with a pattern of its own; each must
# return no-match and leave one line, the last two with their pattern in it.
# The harness then moves the library's clock past the minute, and one more
# failure of a pattern must report the 9999 that were left out.
# Where the PCRE2 build has no Unicode support the (*UTF) verb will not
# compile and the test reports partial.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
. "$(dirname "$0")/../lib/build-harness.sh"

ROOT=$(find_root)

CC=${CC:-cc}
command -v "$CC" >/dev/null 2>&1 || skip "no C compiler available (CC=$CC)"
[ -f "$(xymon_lib "$ROOT" libxymoncomm.a)" ] || skip "tree not built (lib/libxymoncomm.a absent)"

work=$(mktempdir)

require_gnu_make
build_xymon_libs "$ROOT" "$work/libbuild.log" libxymoncomm.a

cat >"$work/harness.c" <<'HARNESS'
#include <stdio.h>
#include "libxymon.h"

#define RUNS 10000

int main(void)
{
	static char plain[] = "(*UTF)^plain.$";
	static char pattern[] = "(*UTF)^caf.$";
	static char name[] = "(*UTF)^host.$";
	pcre2_code *c1, *c2, *c3;
	int i, hits = 0;

	fakestarttime = 1000000000;		/* getcurrenttime() now counts from here */

	c1 = compileregex(plain);		/* the (*UTF) verb needs a Unicode PCRE2 */
	if (!c1) return 2;			/* no Unicode support: test reports partial */
	c2 = compileregex(pattern);
	c3 = compileregex(name);
	if (!c2 || !c3) return 3;

	/* "\351" is Latin-1 e-acute: a lead byte with nothing after it, invalid UTF-8 */
	for (i = 0; (i < RUNS); i++) {
		hits += matchregex("plain\351", c1);
		hits += patternmatch("caf\351", pattern, c2);
		hits += namematch("host\351", name, c3);
	}

	fprintf(stderr, "-- a minute later\n");
	fakestarttime += 61;
	hits += patternmatch("caf\351", pattern, c2);

	printf("hits=%d\n", hits);
	freeregex(c1); freeregex(c2); freeregex(c3);
	return 0;
}
HARNESS

build_harness "$work" "$work/harness" "$work/harness.c" libxymoncomm.a

rc=0
out=$("$work/harness" 2>"$work/err") || rc=$?
if [ "$rc" -eq 2 ]; then
	pass_partial "matchregex() reports a PCRE2 match error as no-match and logs it" \
		"this PCRE2 has no Unicode support, so the (*UTF) trigger does not compile"
fi
[ "$rc" -eq 0 ] || fail "the harness exited $rc: $(sed -n 1,3p "$work/err")"
assert_equal "hits=0" "$out" "a match error is no longer reported as no-match"

err=$(cat "$work/err")
assert_contains "Regexp match failed (" "$err" \
	"matchregex() did not log the PCRE2 match error, so a silently-never-matching rule leaves no trace"
assert_contains "Regexp match failed for '(*UTF)^caf.\$' (" "$err" \
	"patternmatch() did not name the pattern whose match failed"
assert_contains "Regexp match failed for '(*UTF)^host.\$' (" "$err" \
	"namematch() did not name the pattern whose match failed"

lines=$(sed '/^-- a minute later/,$d' "$work/err" | grep -c "Regexp match failed" || true)
assert_equal "3" "$lines" \
	"30000 failed evaluations of three patterns within a minute must leave one log line per pattern"

later=$(sed '1,/^-- a minute later/d' "$work/err")
assert_contains "for '(*UTF)^caf.\$' " "$later" \
	"a pattern still failing a minute later was not reported again"
assert_contains "and 9999 more times since the last report" "$later" \
	"the report a minute later does not count the failures left out in between"

pass "a PCRE2 match error is reported as no-match and logged once a minute per pattern, with the pattern where it is known and the count left out"
