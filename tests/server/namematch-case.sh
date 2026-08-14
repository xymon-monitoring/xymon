#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/namematch-case.sh
#
# Regression guard for namematch() doing case-insensitive comparison of the
# plain (no-wildcard) name list (#182).
#
# namematch(needle, haystack, NULL) tokenises a comma-separated haystack and
# compares each token to needle. #182 switched those comparisons from strcmp()
# to strcasecmp() so that e.g. a hosts.cfg tag list written "CONN" matches the
# test named "conn". Negated tokens ("!name") must fold case the same way.
#
# namematch() is exported from libxymon, so this compiles a tiny harness that
# links the real library and drives the function directly. No wildcard/regex
# path is exercised here (that goes through PCRE and is unchanged); only the
# plain-list branch, which is what #182 touched.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)

CC=${CC:-cc}
command -v "$CC" >/dev/null 2>&1 || skip "no C compiler available (CC=$CC)"
[ -f "$ROOT/lib/libxymoncomm.a" ] || skip "tree not built (lib/libxymoncomm.a absent)"

work=$(mktempdir)

require_gnu_make
"$XYMON_MAKE" -C "$ROOT/lib" libxymoncomm.a >"$work/libbuild.log" 2>&1 \
	|| { cat "$work/libbuild.log" >&2; fail "cannot refresh libxymoncomm.a"; }

pcre_libs=${PCRELIBS:-}
if [ -z "$pcre_libs" ] && command -v pkg-config >/dev/null 2>&1; then
	pcre_libs=$(pkg-config --libs libpcre2-8 2>/dev/null || true)
fi
[ -n "$pcre_libs" ] || pcre_libs="-lpcre2-8"

cat >"$work/harness.c" <<'EOF'
#include <stdio.h>
#include "libxymon.h"

/* namematch() may write into its haystack (strtok_r), so pass writable copies. */
static int nm(const char *needle, const char *haystack)
{
	char buf[256];
	snprintf(buf, sizeof(buf), "%s", haystack);
	return namematch(needle, buf, NULL);
}

int main(void)
{
	int fails = 0;
#define CHECK(needle, haystack, want) do { \
	int got = nm((needle), (haystack)); \
	if (got != (want)) { \
		fprintf(stderr, "namematch(\"%s\", \"%s\") = %d, want %d\n", \
			(needle), (haystack), got, (want)); \
		fails++; \
	} \
} while (0)

	/* Case-insensitivity, both directions (the #182 change). */
	CHECK("conn", "CONN", 1);
	CHECK("CONN", "conn", 1);
	CHECK("Conn", "coNN", 1);
	/* Within a comma list, a later mixed-case token still matches. */
	CHECK("disk", "cpu,DISK,mem", 1);
	/* A genuine non-match must still be a non-match (not everything passes). */
	CHECK("http", "cpu,disk,mem", 0);
	/* Negation folds case the same way: "!CONN" excludes "conn". */
	CHECK("conn", "!CONN", 0);
	/* Exact-case behaviour is unchanged. */
	CHECK("cpu", "cpu", 1);

	if (fails) { fprintf(stderr, "%d namematch case checks failed\n", fails); return 1; }
	return 0;
}
EOF

harness_cflags=$(xymon_cflags "$ROOT")
harness_ldflags=$(xymon_ldflags "$ROOT")
"$CC" $harness_cflags -o "$work/harness" \
	"$work/harness.c" "$ROOT/lib/libxymoncomm.a" $harness_ldflags $pcre_libs 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; fail "harness does not compile"; }

"$work/harness" 2>"$work/run.log" \
	|| fail "namematch case-insensitivity regressed (#182):
$(cat "$work/run.log")"

pass "namematch() compares the plain name list case-insensitively, negation included (#182)"
