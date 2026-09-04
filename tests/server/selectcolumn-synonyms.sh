#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/selectcolumn-synonyms.sh
#
# selectcolumn() (lib/misc.c) finds the columns xymond reads out of a client's
# df header, and now takes a "|"-separated list of alternatives -- macOS sends
# "Avail" or "Available" depending on the client's vintage.
#
# Pinned here, against the real function:
#
#   - a name with no "|" still hits and misses as before
#   - any alternative matches, wherever it sits in the list
#   - matching stays case-insensitive
#   - an empty alternative matches nothing, an empty heading included: that
#     heading reaches the comparison as a zero-length token, so a length test
#     alone would call it a match
#   - whole names only: "Availability" is not "Avail", and "Available" does not
#     match a header that says "Avail"

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
here=$(dirname "$0")

MISC_C="$ROOT/lib/misc.c"
[ -f "$MISC_C" ] || skip "lib/misc.c not present in this checkout"
# The synonym support is what this file guards; its absence is a regression,
# not a green skip.
grep -q "columnmatch" "$MISC_C" \
	|| fail "selectcolumn() no longer has its alternative-name matching (regressed)"

CC=${CC:-cc}
command -v "$CC" >/dev/null 2>&1 || skip "no C compiler available (CC=$CC)"
require_gnu_make

[ -f "$ROOT/include/config.h" ] && [ -f "$ROOT/lib/libxymoncomm.a" ] \
	|| skip "tree not built (run make first; the post-build CI suite covers this)"

work=$(mktempdir)

"$XYMON_MAKE" -C "$ROOT/lib" libxymoncomm.a >"$work/libbuild.log" 2>&1 \
	|| { cat "$work/libbuild.log" >&2; fail "cannot refresh libxymoncomm.a"; }

harness_cflags=$(xymon_cflags "$ROOT")
harness_ldflags=$(xymon_ldflags "$ROOT")
"$CC" $harness_cflags -o "$work/harness" \
	"$here/selectcolumn-synonyms-harness.c" \
	"$ROOT/lib/libxymoncomm.a" $harness_ldflags 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; fail "harness does not compile"; }

out=$("$work/harness")

# Column indexes are 0-based: Filesystem 0, blocks 1, Used 2, free 3, Capacity 4.
assert_contains "plain_hit=4"  "$out" "a name with no alternatives must still be found"
assert_contains "plain_miss=-1" "$out" "a name that is not in the header must still miss"
assert_contains "syn_first=3"  "$out" "the first alternative must match"
assert_contains "syn_second=3" "$out" "the second alternative must match"
assert_contains "syn_third=3"  "$out" "an alternative after the first two must match"
assert_contains "syn_miss=-1"  "$out" "a list where nothing matches must still miss"
assert_contains "case_fold=3"  "$out" "matching must stay case-insensitive"
assert_contains "empty_alt=3"  "$out" \
	"an empty alternative must match nothing, and must not stop the ones after it"
assert_contains "empty_both=-1" "$out" \
	"an empty alternative must not match an empty heading, which nextcolumn() returns as a zero-length token"
assert_contains "empty_hdr=-1" "$out" \
	"an empty heading has no columns to find"
assert_contains "no_prefix=-1" "$out" \
	"a longer heading must not match a shorter alternative (Availability is not Avail)"
assert_contains "no_suffix=-1" "$out" \
	"a longer alternative must not match a shorter heading (Avail is not Available)"

pass "selectcolumn(): alternative column names, matched whole and case-insensitively"
