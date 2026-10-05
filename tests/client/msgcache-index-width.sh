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
ROOT=$(find_root)

CC=${CC:-cc}
command -v "$CC" >/dev/null 2>&1 || skip "no C compiler available (CC=$CC)"

work=$(mktempdir)
printf 'int main(void) { return 0; }\n' > "$work/probe.c"
"$CC" -fsanitize=address -o "$work/probe" "$work/probe.c" 2>/dev/null \
	|| skip "cc does not support -fsanitize=address, without which the overrun is not visible"

# Built the way client/Makefile builds msgcache: against the client archives
# every variant produces, not the server's libxymon.a, which a client tree
# without PCRE cannot build. Both must already be there -- no make from here.
[ -f "$ROOT/lib/libxymonclientcomm.a" ] && [ -f "$ROOT/lib/libxymonclient.a" ] \
	|| skip "the client archives are not built (lib/libxymonclientcomm.a, lib/libxymonclient.a)"

harness_cflags=$(xymon_cflags "$ROOT")
harness_ldflags=$(xymon_ldflags "$ROOT")
pcre_libs=${PCRELIBS:-}
[ -n "$pcre_libs" ] || [ ! -f "$ROOT/Makefile" ] || pcre_libs=$(sed -n 's/^PCRELIBS *= *//p' "$ROOT/Makefile")
if [ -z "$pcre_libs" ] && command -v pkg-config >/dev/null 2>&1; then
	pcre_libs=$(pkg-config --libs libpcre2-8 2>/dev/null || true)
fi
[ -n "$pcre_libs" ] || pcre_libs="-lpcre2-8"
# The client archives use PCRE only in a localclient or server tree, by the
# CLIENTONLY/LOCALCLIENT test lib/loadalerts.h makes; a plain client tree
# links none, and may have none installed.
case $harness_cflags in
	*-DLOCALCLIENT*) ;;
	*-DCLIENTONLY*) pcre_libs= ;;
esac

"$CC" -g -fsanitize=address $harness_cflags -o "$work/index-harness" \
	"$ROOT/tests/client/msgcache-index-width-harness.c" \
	"$ROOT/lib/libxymonclientcomm.a" "$ROOT/lib/libxymonclient.a" \
	$harness_ldflags $pcre_libs 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; fail "the msgcache harness does not build against the client archives"; }

rc=0
ASAN_OPTIONS=detect_leaks=0 "$work/index-harness" > "$work/out" 2> "$work/err" || rc=$?
if grep -q 'stack-buffer-overflow' "$work/err"; then
	fail "grabdata() overran the stack buffer it formats the pull index entry into: $(grep -m1 -E '^ *#[0-9]+ .* in grabdata' "$work/err" || head -3 "$work/err")"
fi
[ "$rc" -eq 0 ] ||
	fail "the msgcache harness did not get the expected index line back (rc=$rc): $(cat "$work/err")"

pass "msgcache grabdata() returns the pull index entry \"$(cat "$work/out")\" whole, with no overrun"
