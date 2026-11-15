#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/web/criticaleditor-escape.sh
#
# The critical systems editor prints what critical.cfg holds escaped for
# HTML. Its resolver group, instructions, last-update line and clone names
# were printed as they were stored, inside the form's VALUE="..." attributes.
# Checked, with the real criticaleditor.cgi:
#
#   - none of the four comes out as markup, and none closes its attribute:
#     each comes out escaped, its '"' as &quot; and its '&' as &amp;;
#   - an ordinary clone name is still listed as it is;
#   - a group with a space keeps a plain space in its VALUE attribute,
#     so a save hands it back unchanged (no &nbsp;).

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
require_c_buildenv "$ROOT"

pcre_libs=${PCRELIBS:-$(xymon_makevar "$ROOT" PCRELIBS)}
if [ -z "$pcre_libs" ] && command -v pkg-config >/dev/null 2>&1; then
	pcre_libs=$(pkg-config --libs libpcre2-8 2>/dev/null || true)
fi
[ -n "$pcre_libs" ] || pcre_libs="-lpcre2-8"

work=$(mktempdir)
build_xymon_libs "$ROOT" "$work/libbuild.log" libxymon.a libxymoncomm.a

harness_cflags=$(xymon_cflags "$ROOT")
harness_ldflags=$(xymon_ldflags "$ROOT")
"$CC" $harness_cflags -o "$work/criticaleditor" "$ROOT/web/criticaleditor.c" \
	"$(xymon_lib "$ROOT" libxymon.a)" "$(xymon_lib "$ROOT" libxymoncomm.a)" "$(xymon_lib "$ROOT" libxymon.a)" \
	$pcre_libs $harness_ldflags 2>"$work/cc.log" \
	|| fail "criticaleditor does not compile: $(cat "$work/cc.log")"

mkdir -p "$work/web" "$work/etc"
cp "$ROOT"/xymond/webfiles/critedit_* "$work/web/"
printf '127.0.0.1 host.example # \n' >"$work/etc/hosts.cfg"

show() {
	OUT=$(REQUEST_METHOD=GET QUERY_STRING="HOSTNAME=host.example&SERVICE=conn&Find=Search" \
		XYMONHOME="$work" HOSTSCFG="!$work/etc/hosts.cfg" \
		"$work/criticaleditor" --config="$work/etc/critical.cfg" 2>/dev/null) \
		|| fail "criticaleditor failed (rc $?)"
}

# group, instructions and last-update line, and a clone: each carries a '&',
# and a '"' that would close the VALUE="..." attribute it is printed into
{
	printf 'host.example|conn||||1|G&1"><b>GROUP</b>|E&1"><b>EXTRA</b>|U&1"><b>UPD</b>\n'
	printf 'C&1"><b>CLONE</b>|=host.example\n'
	printf 'normal.example|=host.example\n'
} >"$work/etc/critical.cfg"
show
for f in GROUP EXTRA UPD CLONE; do
	i=${f:0:1}
	assert_not_contains "<b>$f</b>" "$OUT" "the stored $f value came out as markup"
	assert_not_contains "$i&1\"" "$OUT" "the stored $f value's quote was printed as is: it closes the attribute"
	assert_contains "$i&amp;1&quot;&gt;&lt;b&gt;$f&lt;/b&gt;" "$OUT" "the stored $f value is not shown escaped, & and \" included"
done
assert_contains '<option value="normal.example">normal.example</option>' "$OUT" "an ordinary clone name is no longer listed as it is"

printf 'host.example|conn||||1|Unix team|||x\n' >"$work/etc/critical.cfg"
show
assert_contains 'NAME="GROUP" TYPE="TEXT" SIZE=20 VALUE="Unix team"' "$OUT" "a group with a space does not keep a plain space"

pass "the critical systems editor prints the group, instructions, last update and clone names escaped"
