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

pass "anchorsafe keeps a UTF-8 group title's bytes and reduces the HTML metacharacters"
