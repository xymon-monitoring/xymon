#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/libxymon/rfc2047-decode.sh
#
# rfc2047_decode() (lib/encoding.c) decodes the =?charset?B|Q?text?= words in a
# mail header value (xymon-mailack), carrying the decoded bytes as-is. Check the
# B and Q forms, adjacent-word whitespace folding, literal passthrough, a
# malformed word left verbatim, and -- under AddressSanitizer -- a high byte in
# a B-word's base64, which indexes base64decode's table.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
# shellcheck source=tests/lib/build-harness.sh
. "$(dirname "$0")/../lib/build-harness.sh"

ROOT=$(find_root)
require_c_buildenv "$ROOT"
lib=libxymon.a
[ -f "$(xymon_lib "$ROOT" "$lib")" ] || lib=libxymonclient.a
[ -f "$(xymon_lib "$ROOT" "$lib")" ] \
	|| skip "tree not built (run make first; the post-build CI suite covers this)"

work=$(mktempdir)
san=""
if asan_usable "$ROOT"; then san="-fsanitize=address -g"; fi
build_xymon_libs "$ROOT" "$work/libbuild.log" "$lib" libxymoncomm.a
HARNESS_CFLAGS="-iquote $ROOT/include -iquote $ROOT/lib $san" \
	build_harness "$work" "$work/harness" tests/libxymon/rfc2047-decode-harness.c "$lib" libxymoncomm.a

export LC_ALL=C
dec() { "$work/harness" "$1"; }
eacute=$(printf '\303\251')	# "e'" UTF-8, C3 A9

assert_equal "${eacute}t${eacute}" "$(dec '=?UTF-8?B?w6l0w6k=?=')" \
	"a B encoded-word did not decode to its bytes"
assert_equal "${eacute}t${eacute}" "$(dec '=?UTF-8?Q?=C3=A9t=C3=A9?=')" \
	"a Q encoded-word did not decode to its bytes"
assert_equal "a b" "$(dec '=?UTF-8?Q?a_b?=')" \
	"Q did not turn '_' into a space"
assert_equal "${eacute}${eacute}" "$(dec '=?UTF-8?B?w6k=?= =?UTF-8?B?w6k=?=')" \
	"whitespace between two encoded-words was not dropped"
assert_equal "${eacute} x" "$(dec '=?UTF-8?B?w6k=?= x')" \
	"whitespace between an encoded-word and literal text was dropped"
assert_equal "plain ascii" "$(dec 'plain ascii')" \
	"non-encoded text was not passed through unchanged"
assert_equal '=ZZ' "$(dec '=?UTF-8?Q?=ZZ?=')" \
	"a valid Q word with a bad =XX should emit the bad hex literally"
assert_equal '=?UTF-8?B?QQ' "$(dec '=?UTF-8?B?QQ')" \
	"an unterminated encoded-word (no ?=) should be left verbatim"

# AddressSanitizer: a raw high byte inside a B-word's base64 indexes
# base64decode's table; it must stay in range. 0x80 is the first out-of-range
# index for a 128-entry table.
hib=$(printf '=?x?B?QQ\200QQ?=')
dec "$hib" >/dev/null \
	|| fail "rfc2047_decode faulted on a high byte in a B-word (widen base64decode's table to 256)"
[ -n "$san" ] || pass_partial "high-byte B-word handled (AddressSanitizer unavailable)"

pass "rfc2047_decode decodes B and Q words, folds adjacent-word whitespace, and stays in bounds on a high byte"
