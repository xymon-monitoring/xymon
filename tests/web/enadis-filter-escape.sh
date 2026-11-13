#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/web/enadis-filter-escape.sh
#
# The enable/disable page (enadis.cgi) prints its four filter patterns
# back into the maintenance form escaped for HTML. They were printed as
# sent, inside VALUE="..." attributes. Checked, with the real enadis.cgi:
#
#   - a POST with "apply filters" and a pattern in each of the four fields:
#     none comes out as markup, each comes out escaped, and the quote
#     that would close the attribute is "&quot;";
#   - the pagepath cookie, which a GET takes as the page pattern: the same;
#   - a pattern with a space keeps a plain space, so the next submit sends
#     it back unchanged (no &nbsp;).
#
# The CGI accepts a POST only from its own page, so the request carries
# the referer it checks. xymond is pointed at a closed port: the page is
# shown without a board.

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
"$CC" $harness_cflags -o "$work/enadis" "$ROOT/web/enadis.c" \
	"$(xymon_lib "$ROOT" libxymon.a)" "$(xymon_lib "$ROOT" libxymoncomm.a)" "$(xymon_lib "$ROOT" libxymon.a)" \
	$pcre_libs $harness_ldflags 2>"$work/cc.log" \
	|| fail "enadis does not compile: $(cat "$work/cc.log")"

mkdir -p "$work/web" "$work/etc"
cp "$ROOT"/xymond/webfiles/maint_* "$work/web/"
printf '127.0.0.1 host.example # \n' >"$work/etc/hosts.cfg"

# the environment every request runs in; port 1 is closed, so no board
run_env() {
	env XYMONHOME="$work" HOSTSCFG="!$work/etc/hosts.cfg" \
		XYMSRV=127.0.0.1 XYMONDPORT=1 \
		HTTP_HOST=xymon.example SECURECGIBINURL=/xymon-seccgi \
		HTTP_REFERER=http://xymon.example/xymon-seccgi/enadis.sh \
		"$@"
}

post() {
	OUT=$(printf '%s' "$1" | run_env REQUEST_METHOD=POST \
		CONTENT_TYPE=application/x-www-form-urlencoded CONTENT_LENGTH=${#1} \
		"$work/enadis" 2>"$work/enadis.err") \
		|| fail "enadis failed on the POST (rc $?): $(cat "$work/enadis.err")"
}

get_with_cookie() {
	OUT=$(run_env REQUEST_METHOD=GET QUERY_STRING="" HTTP_COOKIE="pagepath=$1" \
		"$work/enadis" 2>"$work/enadis.err") \
		|| fail "enadis failed on the GET (rc $?): $(cat "$work/enadis.err")"
}

# each field closes its attribute and adds markup; "&" and "'" ride along
payload='%22%3E%3Cb%3EFIELD%3C%2Fb%3E%26%27'
post "go=apply+filters&hostpattern=${payload/FIELD/HOST}&pagepattern=${payload/FIELD/PAGE}&ippattern=${payload/FIELD/IP}&classpattern=${payload/FIELD/CLASS}"
for f in HOST PAGE IP CLASS; do
	assert_not_contains "<b>$f</b>" "$OUT" "the $f pattern came out as markup"
	assert_contains "&quot;&gt;&lt;b&gt;$f&lt;/b&gt;&amp;&#39;" "$OUT" "the $f pattern is not shown escaped"
done

get_with_cookie '"><b>COOKIE</b>'
assert_not_contains "<b>COOKIE</b>" "$OUT" "the pagepath cookie came out as markup"
assert_contains "&quot;&gt;&lt;b&gt;COOKIE&lt;/b&gt;" "$OUT" "the pagepath cookie is not shown escaped"

post "go=apply+filters&hostpattern=web+servers"
assert_contains 'VALUE="web servers"' "$OUT" "a pattern with a space does not keep a plain space"

pass "enadis prints its filter patterns escaped, with a space left as a space"
