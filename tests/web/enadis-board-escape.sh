#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/web/enadis-board-escape.sh
#
# The enable/disable page (enadis.cgi) builds its host and test lists, its
# "currently disabled" table and a JavaScript host/test map from the status
# board, which holds test names and disable messages. These are now escaped
# where the page prints them: HTML for the <option> lists, the table cells
# and the <textarea>, and a JavaScript string literal for the host/test map.
# A value holding "<", ">", "&" or a quote is shown as written, not as
# markup. Checked with the real enadis.cgi and a stand-in xymond that serves
# a board carrying such values.

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
# shellcheck disable=SC2086
"$CC" $harness_cflags -o "$work/enadis" "$ROOT/web/enadis.c" \
	"$(xymon_lib "$ROOT" libxymon.a)" "$(xymon_lib "$ROOT" libxymoncomm.a)" "$(xymon_lib "$ROOT" libxymon.a)" \
	$pcre_libs $harness_ldflags 2>"$work/cc.log" \
	|| fail "enadis does not compile: $(cat "$work/cc.log")"

"$CC" -o "$work/fake-xymond" "$ROOT/tests/lib/fake-xymond.c" 2>"$work/cc-fake.log" \
	|| { cat "$work/cc-fake.log" >&2; fail "fake-xymond does not compile"; }

mkdir -p "$work/web" "$work/etc"
cp "$ROOT"/xymond/webfiles/maint* "$work/web/"
printf '127.0.0.1 host.example # \n' >"$work/etc/hosts.cfg"

# The board enadis fetches: one disabled test on host.example whose test name
# and disable message both carry a quote, an ampersand and markup, including a
# "</textarea>" in the message. Fields: hostname|testname|disabletime|dismsg
future=$(( $(date +%s) + 3600 ))
printf 'host.example|t&1"><b>TST</b>|%s|zz </textarea><b>MSG</b>&"\n' "$future" >"$work/board.reply"

"$work/fake-xymond" "$work/board.reply" >"$work/port" &
fakepid=$!
register_cleanup "stop_process $fakepid"
for _ in 1 2 3 4 5 6 7 8 9 10; do [ -s "$work/port" ] && break; sleep 0.2; done
[ -s "$work/port" ] || fail "fake-xymond did not report its port"

OUT=$(REQUEST_METHOD=GET QUERY_STRING="" \
	XYMONHOME="$work" HOSTSCFG="!$work/etc/hosts.cfg" \
	XYMSRV=127.0.0.1 XYMONDPORT="$(cat "$work/port")" \
	"$work/enadis" 2>"$work/enadis.err") \
	|| fail "enadis failed (rc $?): $(cat "$work/enadis.err")"

# the disable message in the <textarea>: its "</textarea>" must not close it
assert_not_contains "</textarea><b>MSG</b>" "$OUT" "the disable message closed the textarea"
assert_contains "&lt;/textarea&gt;&lt;b&gt;MSG&lt;/b&gt;&amp;&quot;" "$OUT" "the disable message is not HTML-escaped"

# the test name in an <option> (enabletest list): no raw attribute break
assert_not_contains '"><b>TST</b>' "$OUT" "the test name broke out of its option attribute"
assert_contains "t&amp;1&quot;&gt;&lt;b&gt;TST&lt;/b&gt;" "$OUT" "the test name is not HTML-escaped"

# the test name in the JavaScript array: a JS-string escape, not markup
assert_contains 'hosts["host.example"]' "$OUT" "the JavaScript host map is missing"
assert_contains 't&1\"\x3e\x3cb\x3eTST\x3c/b\x3e' "$OUT" "the test name is not escaped for a JavaScript string"
assert_not_contains '"t&1"><b>TST</b>"' "$OUT" "the test name is a raw JS string literal"

pass "enadis escapes the board's test names and disable messages for HTML and for JavaScript"
