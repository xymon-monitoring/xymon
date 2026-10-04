#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/client/msgcache-index-width.sh
#
# msgcache's pull index entry fits its buffer, however wide the entry.
#
# grabdata() formats each queued message's entry, "<length>:<age> ", into a
# stack buffer. The age is now - tstamp, so a clock stepped back after a
# message was queued makes it a large negative number: a 7-digit length and
# an age near -1.79e9, a clock reset to the epoch, need 21 bytes, and the
# buffer was 20, written with an unbounded sprintf().
#
# The harness runs the real grabdata() on a pull request with such a message
# queued, under AddressSanitizer, and requires the exact index line back.
# With the old buffer ASan stops the overrun in grabdata(). Without ASan an
# overrun this small is not reliably visible, so the test skips there.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
# shellcheck source=tests/lib/build-worker.sh
. "$(dirname "$0")/../lib/build-worker.sh"

CC=${CC:-cc}
command -v "$CC" >/dev/null 2>&1 || skip "no C compiler available (CC=$CC)"

work=$(mktempdir)
printf 'int main(void) { return 0; }\n' > "$work/probe.c"
"$CC" -fsanitize=address -o "$work/probe" "$work/probe.c" 2>/dev/null \
	|| skip "cc does not support -fsanitize=address, without which the overrun is not visible"

BUILD_WORKER_CFLAGS="-g -fsanitize=address" \
	build_xymond_worker "$work" index-harness tests/client/msgcache-index-width-harness.c

rc=0
ASAN_OPTIONS=detect_leaks=0 "$work/index-harness" > "$work/out" 2> "$work/err" || rc=$?
if grep -q 'stack-buffer-overflow' "$work/err"; then
	fail "grabdata() overran the stack buffer it formats the pull index entry into: $(grep -m1 -E '^ *#[0-9]+ .* in grabdata' "$work/err" || head -3 "$work/err")"
fi
[ "$rc" -eq 0 ] ||
	fail "the msgcache harness did not get the expected index line back (rc=$rc): $(cat "$work/err")"

pass "msgcache grabdata() returns the pull index entry \"$(cat "$work/out")\" whole, with no overrun"
