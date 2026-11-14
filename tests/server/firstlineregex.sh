#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/firstlineregex.sh
#
# firstlineregex() and firstlineregexnocase() compile with PCRE2_FIRSTLINE: a
# match must start in the subject's first line. They tested PCRE_FIRSTLINE,
# which pcre2.h does not define, so since the PCRE2 migration (#5) the flag
# was dropped without a word and firstlineregexnocase() named PCRE_CASELESS,
# which does not compile at all.
#
# Both macros are expressions, with no trailing ';' of their own, so each
# can be the body of an if that has an else; the harness uses them so.
#
# Checked through a harness that links the real library: "abc" compiled by
# each macro matches "abc\nx" and not "x\nabc", where it starts on line two.

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

pcre_libs=${PCRELIBS:-}
if [ -z "$pcre_libs" ] && command -v pkg-config >/dev/null 2>&1; then
	pcre_libs=$(pkg-config --libs libpcre2-8 2>/dev/null || true)
fi
[ -n "$pcre_libs" ] || pcre_libs="-lpcre2-8"

cat >"$work/harness.c" <<'HARNESS'
#include <stdio.h>
#include "libxymon.h"

int main(void)
{
	int fails = 0;
	pcre2_code *cs, *nc;

	if (fails == 0) cs = firstlineregex("abc"); else cs = NULL;
	if (fails == 0) nc = firstlineregexnocase("ABC"); else nc = NULL;
	if (!cs || !nc) { fprintf(stderr, "a firstline regex did not compile\n"); return 1; }

#define CHECK(name, code, subject, want) do { \
	int got = matchregex((subject), (code)); \
	if (got != (want)) { \
		fprintf(stderr, "%s on \"%s\" = %d, want %d\n", (name), (subject), got, (want)); \
		fails++; \
	} \
} while (0)

	CHECK("firstlineregex(\"abc\")", cs, "abc\nx", 1);
	CHECK("firstlineregex(\"abc\")", cs, "x\nabc", 0);
	CHECK("firstlineregexnocase(\"ABC\")", nc, "abc\nx", 1);
	CHECK("firstlineregexnocase(\"ABC\")", nc, "x\nabc", 0);

	freeregex(cs);
	freeregex(nc);
	return fails ? 1 : 0;
}
HARNESS

build_harness "$work" "$work/harness" \
	"$work/harness.c" libxymoncomm.a

"$work/harness" 2>"$work/run.log" \
	|| fail "a firstline regex matched past the first line:
$(cat "$work/run.log")"

pass "firstlineregex() and firstlineregexnocase() only match from the first line"
