#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/libxymon/matching-firstlineregex.sh
#
# firstlineregex() and firstlineregexnocase() (lib/matching.h) must compile
# their pattern with PCRE2_FIRSTLINE, so a match has to start on the first
# line of the subject. After the PCRE2 port both macros still tested
# #ifdef PCRE_FIRSTLINE, which pcre2.h never defines: firstlineregex()
# silently compiled with no flag at all and matched anywhere in the
# subject, and firstlineregexnocase() referred to the removed PCRE_CASELESS
# and did not compile.
#
# The assertions are about what only the flag produces: an unanchored
# pattern that appears on the second line must NOT match. Compiles the real
# lib/matching.c against a minimal libxymon.h shim (the real lib/matching.h,
# plus stubs for the logging and SLA calls matching.c makes), so the test
# needs no configured or built tree and runs in every variant.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)

[ -f "$ROOT/lib/matching.c" ] || skip "lib/matching.c not present in this checkout"

CC=${CC:-cc}
command -v "$CC" >/dev/null 2>&1 || skip "no C compiler available (CC=$CC)"

work=$(mktemp -d "${TMPDIR:-/tmp}/xymon-firstline.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM

pcre_inc=$(pcre_cflags "$ROOT")
pcre_libs=${PCRELIBS:-}
if [ -z "$pcre_libs" ] && command -v pkg-config >/dev/null 2>&1; then
        pcre_libs=$(pkg-config --libs libpcre2-8 2>/dev/null || true)
fi
[ -n "$pcre_libs" ] || pcre_libs=-lpcre2-8

# PCRE2 itself is a host dependency: skip, not fail, where it is absent.
printf '#define PCRE2_CODE_UNIT_WIDTH 8\n#include <pcre2.h>\nint main(void){return 0;}\n' >"$work/probe.c"
# shellcheck disable=SC2086  # flag lists are meant to split
"$CC" $pcre_inc -o "$work/probe" "$work/probe.c" $pcre_libs 2>/dev/null \
        && "$work/probe" \
        || skip "PCRE2 (pcre2.h / libpcre2-8) not available"

# matching.c includes "libxymon.h"; the shim directory is searched before
# anything else, so it supplies that name. It includes the real
# lib/matching.h, which is the header under test.
mkdir -p "$work/shim"
cat >"$work/shim/libxymon.h" <<'SHIM'
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "matching.h"
#define xfree(p) do { free(p); (p) = NULL; } while (0)
void dbgprintf(const char *fmt, ...);
void errprintf(const char *fmt, ...);
int within_sla(char *holidaykey, char *timespec, int defresult);
SHIM

cat >"$work/harness.c" <<'HARNESS'
#include <stdio.h>
#include <stdarg.h>
#include "libxymon.h"

void dbgprintf(const char *fmt, ...) { (void)fmt; }
void errprintf(const char *fmt, ...)
{
        va_list ap;
        va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap);
}
int within_sla(char *holidaykey, char *timespec, int defresult)
{
        (void)holidaykey; (void)timespec; return defresult;
}

static int failures = 0;

static void expect(const char *label, int cond)
{
        if (!cond) { fprintf(stderr, "%s\n", label); failures++; }
}

int main(void)
{
        pcre2_code *re;

        re = firstlineregex("foo");
        if (!re) { fprintf(stderr, "firstlineregex(\"foo\") did not compile\n"); return 2; }
        expect("firstlineregex(\"foo\") must match \"foo\" on the first line",
               matchregex("foo bar", re));
        expect("firstlineregex(\"foo\") must not match \"foo\" on the second line "
               "(PCRE2_FIRSTLINE not set)",
               !matchregex("line1\nfoo", re));
        freeregex(re);

        re = firstlineregexnocase("foo");
        if (!re) { fprintf(stderr, "firstlineregexnocase(\"foo\") did not compile\n"); return 2; }
        expect("firstlineregexnocase(\"foo\") must match \"FOO\" on the first line "
               "(PCRE2_CASELESS not set)",
               matchregex("FOO bar", re));
        expect("firstlineregexnocase(\"foo\") must not match \"FOO\" on the second line "
               "(PCRE2_FIRSTLINE not set)",
               !matchregex("line1\nFOO", re));
        freeregex(re);

        return failures ? 1 : 0;
}
HARNESS

# shellcheck disable=SC2086  # flag lists are meant to split
"$CC" -iquote "$work/shim" -iquote "$ROOT/lib" $pcre_inc -o "$work/harness" \
        "$work/harness.c" "$ROOT/lib/matching.c" $pcre_libs 2>"$work/cc.log" \
        || { cat "$work/cc.log" >&2; fail "harness does not compile against lib/matching.c and lib/matching.h"; }

"$work/harness" 2>"$work/stderr.log" \
        || fail "firstlineregex assertions failed:
$(cat "$work/stderr.log")"

pass "firstlineregex() and firstlineregexnocase() only match on the first line, and the nocase form matches case-insensitively"
