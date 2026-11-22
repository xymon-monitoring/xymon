#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/libxymon/anchorsafe.sh
#
# anchorsafe() (lib/misc.c) reduces a group title to an <A NAME> jump target
# (xymongen/pagegen.c). Unlike stripnonwords() it keeps a byte >= 0x80, so an
# accented title stays distinct and a non-Latin one does not collapse to an
# empty anchor -- while the HTML metacharacters < > & " still become one
# underscore, keeping the result safe in a NAME= and a SUMMARY= attribute.

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
build_xymon_libs "$ROOT" "$work/libbuild.log" "$lib" libxymoncomm.a
HARNESS_CFLAGS="-iquote $ROOT/include -iquote $ROOT/lib" \
	build_harness "$work" "$work/harness" tests/libxymon/anchorsafe-harness.c "$lib" libxymoncomm.a

export LC_ALL=C	# compare bytes, whatever the locale

# anchor VALUE -- what anchorsafe makes of VALUE
anchor() { "$work/harness" "$1"; }

eacute=$(printf '\303\251')          # "e'" in UTF-8, C3 A9
cyr=$(printf '\320\241\320\265\321\202\321\214')  # "Сеть" (Cyrillic), all >= 0x80

# An accented title keeps its bytes, not one underscore per byte.
assert_equal "R${eacute}seau" "$(anchor "R${eacute}seau")" \
	"an accented group title lost its non-ASCII bytes in the anchor"

# A wholly non-Latin title is a non-empty anchor, not "".
assert_equal "$cyr" "$(anchor "$cyr")" \
	"a non-Latin group title collapsed to an empty anchor"

# ASCII word characters are unchanged; a space becomes one underscore.
assert_equal "Web_Servers" "$(anchor "Web Servers")" \
	"an ASCII group title no longer reduces to word characters and underscores"

# HTML metacharacters still become a single underscore -- safe in NAME=/SUMMARY=.
assert_equal "A_B_C" "$(anchor 'A<B>&"C')" \
	"an HTML metacharacter was kept in the anchor instead of reduced to an underscore"

# The 250-byte cap must not split a character. 249 'a' then "e'" (C3 A9): the
# cap falls after the C3, and the anchor must drop the whole character, not end
# in a lone 0xC3. One 'a' fewer (248) and the character fits and is kept whole.
a249=$(awk 'BEGIN { for (i = 0; i < 249; i++) printf "a" }')
a248=$(awk 'BEGIN { for (i = 0; i < 248; i++) printf "a" }')
assert_equal "$a249" "$(anchor "${a249}${eacute}")" \
	"the 250-byte cap split a character, leaving a partial sequence in the anchor"
assert_equal "${a248}${eacute}" "$(anchor "${a248}${eacute}")" \
	"a character that fits within the cap was dropped"

# A non-UTF-8 run of 0x80-0xBF bytes (Latin-1 0xB0, degree sign) is not a split
# UTF-8 character: the back-up is bounded to three and only drops a real lead
# byte, so it must leave the run and the "lat" before it alone, keeping 250.
b0=$(awk 'BEGIN { printf "lat"; for (i = 0; i < 300; i++) printf "%c", 176 }')
b0got=$(anchor "$b0")
[ "${#b0got}" -eq 250 ] \
	|| fail "a non-UTF-8 0xB0 run was treated as a split character: anchor is ${#b0got} bytes, not 250"
case "$b0got" in lat*) : ;; *) fail "the back-up ate into 'lat' before a non-UTF-8 0xB0 run" ;; esac

pass "anchorsafe keeps a UTF-8 group title's bytes and reduces the HTML metacharacters"
