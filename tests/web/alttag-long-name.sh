#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/web/alttag-long-name.sh
#
# alttag() builds the ALT/TITLE text of a status icon -- "COLUMN:COLOR:", then
# "acked:", "nopropagate:" and the age when there is room -- in a buffer of
# 1024 bytes, counting down the room left. snprintf() returns the length it
# would have written, so for a column name too long for the buffer the count
# went below zero, and the parts after the name were added as if there were
# room.
#
# Checked at both sides of the buffer: a short name gets every part; a name of
# 1010 bytes is cut before them, as before; names of 1020 and 1100 bytes must
# give a cut text too, no longer than the buffer, and the process must not be
# stopped. htmllog.c is compiled into the harness with AddressSanitizer where
# the compiler has it, which checks every write against the buffer; elsewhere a
# build with _FORTIFY_SOURCE checks the strncat() calls.

set -euo pipefail
. "$(dirname "$0")/../lib/assert.sh"
. "$(dirname "$0")/../lib/build-harness.sh"

ROOT=$(find_root)
here=$(dirname "$0")

require_c_buildenv "$ROOT"
[ -f "$(xymon_lib "$ROOT" libxymon.a)" ] \
	|| skip "tree not built (run make first; the post-build CI suite covers this)"

work=$(mktempdir)
build_xymon_libs "$ROOT" "$work/libbuild.log" libxymon.a libxymoncomm.a
san=""
if asan_usable; then san="-fsanitize=address -fno-omit-frame-pointer"; fi
HARNESS_CFLAGS="$san" build_harness "$work" "$work/harness" \
	"$here/alttag-long-name-harness.c" "$ROOT/lib/htmllog.c" libxymon.a libxymoncomm.a libxymon.a

tag() {
	local rc=0
	OUT=$(ASAN_OPTIONS=detect_leaks=0 "$work/harness" "$1" 2>"$work/err") || rc=$?
	[ "$rc" -eq 0 ] || fail "alttag() with a column name of $1 bytes stopped the process (exit $rc): $(head -5 "$work/err")"
	LEN=${OUT%% *}
}

tag 100
assert_contains ":red:acked:nopropagate:3 days 01:02:03" "$OUT" "a short column name does not get every part of its ALT text"

for n in 1010 1020 1100; do
	tag "$n"
	[ "$LEN" -le 1023 ] || fail "the ALT text for a column name of $n bytes is $LEN bytes, longer than its buffer"
	assert_not_contains "acked:" "$OUT" "the ALT text for a column name of $n bytes, already cut, was given the parts after the name"
done

claim="alttag() gives a short column name its whole ALT text, and a name too long for the buffer a cut one"
[ -n "$san" ] || pass_partial "$claim" "no AddressSanitizer here: the writes are checked only where _FORTIFY_SOURCE is on"
pass "$claim"
