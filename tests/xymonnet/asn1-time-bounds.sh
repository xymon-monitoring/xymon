#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/xymonnet/asn1-time-bounds.sh
#
# xymonnet formats a certificate's validity times without reading past them.
#
# xymon_ASN1_UTCTIME() read the two seconds digits whether or not the time had
# them: for YYMMDDHHMM, ten characters, it read one byte past the end. That
# byte was OpenSSL's terminating NUL, so nothing showed; OpenSSL 4.1 no longer
# promises it, and its ASan and Valgrind builds report the read.
#
# The harness gives the function each time in exactly as many bytes as it
# has, under AddressSanitizer, and requires every one formatted. With the old
# read ASan stops it on the ten-character time. Without ASan a one-byte read
# past a heap block is not reliably visible, so the test skips there.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
ROOT=$(find_root)

CC=${CC:-cc}
command -v "$CC" >/dev/null 2>&1 || skip "no C compiler available (CC=$CC)"

work=$(mktempdir)
printf 'int main(void) { return 0; }\n' > "$work/probe.c"
"$CC" -fsanitize=address -o "$work/probe" "$work/probe.c" 2>/dev/null \
	|| skip "cc does not support -fsanitize=address, without which the read past the end is not visible"
"$work/probe" >/dev/null 2>&1 \
	|| skip "an -fsanitize=address program does not start here (no ASan runtime library), without which the read past the end is not visible"

# Built as xymonnet/Makefile builds xymonnetprobe: contest.c with the objects
# and archives it links. All must already be there -- no make from here.
for f in xymonnet/httptest.o xymonnet/dns.o xymonnet/dns2.o xymonnet/httpcookies.o; do
	[ -f "$ROOT/$f" ] || skip "xymonnet is not built ($f missing)"
done
[ -f "$(xymon_lib "$ROOT" libxymoncomm.a)" ] && [ -f "$(xymon_lib "$ROOT" libxymontime.a)" ] \
	|| skip "the server archives are not built (lib/libxymoncomm.a, lib/libxymontime.a)"
sslflags=$(xymon_makevar "$ROOT" SSLFLAGS)
case $sslflags in
	*-DHAVE_OPENSSL*) ;;
	*) skip "built without OpenSSL: xymon_ASN1_UTCTIME() is not compiled" ;;
esac

harness_cflags=$(xymon_cflags "$ROOT")
harness_ldflags=$(xymon_ldflags "$ROOT")
# shellcheck disable=SC2046  # each make variable is a list of words
"$CC" -g -fsanitize=address $harness_cflags $sslflags $(xymon_makevar "$ROOT" SSLINCDIR) \
	$(xymon_makevar "$ROOT" CARESINCDIR) -o "$work/harness" \
	"$ROOT/tests/xymonnet/asn1-time-bounds-harness.c" \
	"$ROOT/xymonnet/httptest.o" "$ROOT/xymonnet/dns.o" "$ROOT/xymonnet/dns2.o" "$ROOT/xymonnet/httpcookies.o" \
	$(xymon_makevar "$ROOT" CARESLIBS) "$(xymon_lib "$ROOT" libxymoncomm.a)" "$(xymon_lib "$ROOT" libxymontime.a)" \
	$(xymon_makevar "$ROOT" ZLIBLIBS) $(xymon_makevar "$ROOT" SSLLIBS) $(xymon_makevar "$ROOT" NETLIBS) \
	$(xymon_makevar "$ROOT" LIBRTDEF) $harness_ldflags 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; fail "the harness does not build against xymonnet's objects"; }

rc=0
ASAN_OPTIONS=detect_leaks=0 "$work/harness" > "$work/out" 2> "$work/err" || rc=$?
if grep -q 'heap-buffer-overflow' "$work/err"; then
	fail "xymon_ASN1_UTCTIME() read past the end of a certificate time: $(grep -m1 -E '^ *#[0-9]+ .* in xymon_ASN1_UTCTIME' "$work/err" || head -3 "$work/err")"
fi
[ "$rc" -eq 0 ] || fail "the harness failed (rc=$rc): $(cat "$work/err")"

assert_equal "2026-03-15 12:30:00 |
2026-03-15 12:30:00 GMT|
2026-03-15 12:30:45 GMT|
2026-03-15 12:30:45 GMT|" "$(cat "$work/out")" "a certificate time was formatted differently"

pass "xymonnet formats certificate times of 10 to 15 characters without reading past them"
