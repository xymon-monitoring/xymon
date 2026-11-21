#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/libxymon/quote-table-size.sh
#
# The escapers in lib/strfunc.c (htmlquoted() and the others) share quote(),
# which copies its table's characters into a 16-byte set for strcspn(). A
# table of 15 entries escapes every one of them. A larger table must stop
# the program with a message and abort(), whatever the input, NULL included: before, a
# 16th entry put the set's terminating NUL past its end, and more entries
# their characters too; stopping short instead would leave the last ones
# unescaped.
#
# The harness includes strfunc.c to reach the static quote(), and is built
# with AddressSanitizer where the compiler has it.

set -euo pipefail
. "$(dirname "$0")/../lib/assert.sh"
. "$(dirname "$0")/../lib/build-harness.sh"

ROOT=$(find_root)
here=$(dirname "$0")

require_c_buildenv "$ROOT"
lib=libxymon.a
[ -f "$(xymon_lib "$ROOT" "$lib")" ] || lib=libxymonclient.a
[ -f "$(xymon_lib "$ROOT" "$lib")" ] \
	|| skip "tree not built (run make first; the post-build CI suite covers this)"

work=$(mktempdir)
build_xymon_libs "$ROOT" "$work/libbuild.log" "$lib"
san=""
if asan_usable; then san="-fsanitize=address -fno-omit-frame-pointer"; fi
HARNESS_CFLAGS="-iquote $ROOT/include -iquote $ROOT/lib $san" \
	build_harness "$work" "$work/harness" "$here/quote-table-size-harness.c" "$lib"

out=$(ASAN_OPTIONS=detect_leaks=0 "$work/harness" 15 2>"$work/err") \
	|| fail "a table of 15 entries stopped quote(): $(head -3 "$work/err")"
assert_equal "<a><b><c><d><e><f><g><h><i><j><k><l><m><n><o>pqrstuvwxyz" "$out" \
	"a table of 15 entries does not escape each of its characters"

rc=0
out=$(ASAN_OPTIONS=detect_leaks=0 "$work/harness" 16 2>"$work/err") || rc=$?
[ "$rc" -eq 134 ] || fail "a table of 16 entries did not abort quote() (exit $rc, abort is 134): it printed '$out'"
assert_contains "quote: a substitution table has more than 15 entries" "$(cat "$work/err")" \
	"a table of 16 entries stopped quote() without saying why"

rc=0
out=$(ASAN_OPTIONS=detect_leaks=0 "$work/harness" 16 null 2>"$work/err") || rc=$?
[ "$rc" -eq 134 ] || fail "a table of 16 entries with a NULL input did not abort quote() (exit $rc, abort is 134): it returned '$out'"
assert_contains "quote: a substitution table has more than 15 entries" "$(cat "$work/err")" \
	"a table of 16 entries with a NULL input stopped quote() without saying why"
out=$(ASAN_OPTIONS=detect_leaks=0 "$work/harness" 15 null 2>"$work/err") \
	|| fail "a table of 15 entries with a NULL input stopped quote(): $(head -3 "$work/err")"
assert_equal "(NULL)" "$out" "quote() no longer returns NULL for a NULL input with nullisnull set"

claim="quote() escapes a table of 15 entries, and stops with a message on a larger one, NULL input included"
[ -n "$san" ] || pass_partial "$claim" "no AddressSanitizer here: a copy past the set would go unreported"
pass "$claim"
