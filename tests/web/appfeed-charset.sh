#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/web/appfeed-charset.sh
#
# appfeed.cgi declares its XML in the charset the web pages declare, and a
# UTF-8 answer stays well-formed whatever bytes a host sent. With a real
# xymond holding statuses with UTF-8 text, a lone Latin-1 byte, U+FFFF, an
# overlong form and a cut-off sequence, checked:
#
#   - with HTMLCONTENTTYPE naming no charset, or charset=UTF-8, the answer is
#     declared UTF-8, in the HTTP header and in the XML declaration;
#   - well-formed UTF-8 text comes through as sent;
#   - a byte that is not part of well-formed UTF-8, U+FFFF, an overlong form
#     and a sequence cut off at the end each become U+FFFD, so the answer
#     stays well-formed;
#   - with charset=ISO-8859-1, the answer is declared ISO-8859-1 and every
#     byte comes through as sent, as before this change;
#   - a charset name holding anything but a name's characters, or not
#     starting with a letter, is not used, nor a charset= ending another
#     parameter's name;
#   - a host name holding < > & and a Latin-1 byte is escaped, so the answer
#     stays well-formed.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
# shellcheck source=tests/lib/xymond-daemon.sh
. "$(dirname "$0")/../lib/xymond-daemon.sh"

ROOT=$(find_root)
require_c_buildenv "$ROOT"
require_bin XYMOND xymond/xymond
require_bin XYMONCLIENT common/xymon
require_shm_segments "$(grep -c 'setup_channel(C_[A-Z_]*, CHAN_MASTER)' "$ROOT/xymond/xymond.c")"

pcre_libs=${PCRELIBS:-$(xymon_makevar "$ROOT" PCRELIBS)}
[ -n "$pcre_libs" ] || pcre_libs="-lpcre2-8"

work=$(mktempdir)
build_xymon_libs "$ROOT" "$work/libbuild.log" libxymon.a libxymoncomm.a
cflags=$(xymon_cflags "$ROOT")
ldflags=$(xymon_ldflags "$ROOT")
# shellcheck disable=SC2086  # the flag lists are meant to split
"$CC" $cflags -o "$work/appfeed" "$ROOT/web/appfeed.c" \
	"$(xymon_lib "$ROOT" libxymon.a)" "$(xymon_lib "$ROOT" libxymoncomm.a)" "$(xymon_lib "$ROOT" libxymon.a)" \
	$pcre_libs $ldflags 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; fail "appfeed does not compile"; }

mkdir -p "$work/etc" "$work/home/etc" "$work/home/tmp"
{
	printf '127.0.0.1 web01-example.com # \n'
	printf '127.0.0.1 web01.example.com # \n'
	printf '127.0.0.1 g<h>&caf\351.example.com # \n'
} >"$work/etc/hosts.cfg"
require_cfg XYMONSERVER_CFG xymond/etcfiles/xymonserver.cfg
sed -e 's|^XYMONHOME=.*|XYMONHOME="'"$work"'/home"|' \
    -e 's|^XYMONTMP=.*|XYMONTMP="'"$work"'/home/tmp"|' \
	"$XYMONSERVER_CFG" > "$work/xymond.cfg"

XYMOND_PID=""
stop_xymond() {
	[ -n "$XYMOND_PID" ] || return 0
	kill "$XYMOND_PID" 2>/dev/null || true
	wait "$XYMOND_PID" 2>/dev/null || true
	XYMOND_PID=""
}
register_cleanup stop_xymond

xymond_launch() {
	local port=$1; shift
	"$XYMOND" --no-daemon --listen="127.0.0.1:$port" \
		--hosts="$work/etc/hosts.cfg" --env="$work/xymond.cfg" \
		--pidfile="$work/xymond.pid" --checkpoint-file="$work/chk.out" \
		"$@" \
		> "$work/xymond.log" 2>&1 &
	XYMOND_PID=$!
}

start_xymond


export LC_ALL=C	# compare bytes, whatever the locale

x() { XYMON_TIMEOUT=5 "$XYMONCLIENT" "127.0.0.1:$PORT" "$1"; }

# ok:  "données" in UTF-8. bad: a Latin-1 é, then U+FFFF, an overlong "/"
# (C0 AF) and a three-byte one (E0 80 AF), a surrogate (ED A0 80), a code
# point past U+10FFFF (F4 90 80 80), and a two-byte sequence cut off at the
# end of the text
x "$(printf 'status web01,example,com.conn red donn\303\251es ok')" >/dev/null
x "$(printf 'status g<h>&caf\351,example,com.conn red ghost')" >/dev/null
x "$(printf 'status web01-example,com.conn red caf\351; ff\357\277\277; ol\300\257; o3\340\200\257; su\355\240\200; hi\364\220\200\200; cut\303')" >/dev/null
for _ in $(seq 1 50); do
	got=$(x 'xymondboard color=red fields=hostname')
	case $got in *web01-example*g*) break ;; *g*web01-example*) break ;; esac
	sleep 0.1
done
case $got in
	*web01-example*g*|*g*web01-example*) ;;
	*) fail "xymond did not take the test statuses: '$got'" ;;
esac

feed() {	# HTMLCONTENTTYPE -- the plain feed, in OUT
	printf 'XYMONDPORT="%s"\nXYMSRV="127.0.0.1"\nXYMONSERVERCGIURL="/xymon-cgi"\nHTMLCONTENTTYPE="%s"\n' \
		"$PORT" "$1" > "$work/cgi.cfg"
	set +e
	OUT=$(env REQUEST_METHOD=GET QUERY_STRING= "$work/appfeed" --env="$work/cgi.cfg" 2>&1)
	RC=$?
	set -e
	[ "$RC" -eq 0 ] || fail "appfeed failed (exit $RC) with HTMLCONTENTTYPE '$1'"
}
FFFD=$'\357\277\275'

for ct in "text/html" "text/html; charset=UTF-8" "text/html; charset=utf-8"; do
	feed "$ct"
	assert_contains "Content-type: text/xml; charset=" "$OUT" "'$ct': no charset in the HTTP header"
	case $OUT in
		*"Content-type: text/xml; charset=UTF-8"*|*"Content-type: text/xml; charset=utf-8"*) ;;
		*) fail "'$ct': the HTTP header does not say UTF-8" ;;
	esac
	case $OUT in
		*"<?xml version='1.0' encoding='UTF-8'?>"*|*"<?xml version='1.0' encoding='utf-8'?>"*) ;;
		*) fail "'$ct': the XML is not declared UTF-8" ;;
	esac
	assert_contains $'donn\303\251es ok' "$OUT" "'$ct': well-formed UTF-8 did not come through as sent"
	assert_contains "caf$FFFD; ff$FFFD; ol$FFFD$FFFD; o3$FFFD$FFFD$FFFD; su$FFFD$FFFD$FFFD; hi$FFFD$FFFD$FFFD$FFFD; cut$FFFD" "$OUT" \
		"'$ct': a byte that is not well-formed UTF-8 was not shown as U+FFFD"
	assert_contains "<Servername>g&lt;h&gt;&amp;caf$FFFD.example.com</Servername>" "$OUT" \
		"'$ct': a host name holding < > & and a Latin-1 byte was not escaped"
	case $OUT in
		*"<Servername>g<h>"*) fail "'$ct': a host name put markup in the XML" ;;
	esac
	case $OUT in
		*$'caf\351'*|*$'\357\277\277'*|*$'\300\257'*|*$'\340\200'*|*$'\355\240'*|*$'\364\220'*) fail "'$ct': a byte that is not well-formed UTF-8 reached the XML" ;;
	esac
done

feed "text/html; charset=ISO-8859-1"
assert_contains "Content-type: text/xml; charset=ISO-8859-1" "$OUT" "ISO-8859-1: the HTTP header does not say so"
assert_contains "<?xml version='1.0' encoding='ISO-8859-1'?>" "$OUT" "ISO-8859-1: the XML is not declared so"
assert_contains $'caf\351; ff\357\277\277; ol\300\257; o3\340\200\257; su\355\240\200; hi\364\220\200\200; cut\303' "$OUT" "ISO-8859-1: the bytes were not kept as sent"

assert_contains $'<Servername>g&lt;h&gt;&amp;caf\351.example.com</Servername>' "$OUT" \
	"ISO-8859-1: a host name was not escaped, or its bytes not kept"

feed "text/html; xcharset=ISO-8859-1"
assert_contains "<?xml version='1.0' encoding='UTF-8'?>" "$OUT" "the end of another parameter's name was taken for charset="
feed "text/html; charset=_x"
assert_contains "<?xml version='1.0' encoding='UTF-8'?>" "$OUT" "a charset name not starting with a letter was used"
feed "text/html; charset=\"x'><y"
assert_contains "<?xml version='1.0' encoding='UTF-8'?>" "$OUT" "a charset that is not a name was used"
feed "text/html; charset=\"ISO-8859-1'"
assert_contains "<?xml version='1.0' encoding='UTF-8'?>" "$OUT" "a charset with mismatched quotes was used"

pass "appfeed declares the pages' charset, and a UTF-8 answer stays well-formed whatever bytes a host sent"
