#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/web/xml-encoding-charset.sh
#
# The XML that appfeed.cgi and xymond's own xymondxboard reply emit declared
# "encoding='ISO-8859-1'" with a fixed string, while the status text they carry
# is whatever the hosts send -- UTF-8 by default. An XML consumer then decoded
# the UTF-8 as Latin-1 and showed mojibake. The declaration now names the
# charset from HTMLCONTENTTYPE, the same one the HTML pages are served with.
#
# xymond is started with a distinctive HTMLCONTENTTYPE charset (iso-8859-15),
# so the daemon reply must carry that, not the old literal. appfeed, a separate
# process, is then run with its own HTMLCONTENTTYPE values: a UTF-8 one, and
# one with no charset at all (which must fall back to utf-8).

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
printf '127.0.0.1 web01.example.com # \n' >"$work/etc/hosts.cfg"
require_cfg XYMONSERVER_CFG xymond/etcfiles/xymonserver.cfg
# Start xymond with a distinctive charset, so its reply cannot read as the old
# literal or as the utf-8 default.
sed -e 's|^XYMONHOME=.*|XYMONHOME="'"$work"'/home"|' \
    -e 's|^XYMONTMP=.*|XYMONTMP="'"$work"'/home/tmp"|' \
    -e 's|^HTMLCONTENTTYPE=.*|HTMLCONTENTTYPE="text/html; charset=iso-8859-15"|' \
	"$XYMONSERVER_CFG" > "$work/xymond.cfg"
grep -q '^HTMLCONTENTTYPE=' "$work/xymond.cfg" \
	|| printf 'HTMLCONTENTTYPE="text/html; charset=iso-8859-15"\n' >> "$work/xymond.cfg"

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
		"$@" > "$work/xymond.log" 2>&1 &
	XYMOND_PID=$!
}

start_xymond

x() { XYMON_TIMEOUT=5 "$XYMONCLIENT" "127.0.0.1:$PORT" "$1"; }
x "status web01,example,com.conn red Host not responding" >/dev/null
for _ in $(seq 1 50); do
	case $(x 'xymondboard host=^web01.example.com$ test=^conn$ fields=color') in
		*red*) break ;;
	esac
	sleep 0.1
done

# 1. The daemon reply reflects xymond's own HTMLCONTENTTYPE.
board=$(x "xymondxboard")
assert_contains "<?xml version='1.0' encoding='iso-8859-15'?>" "$board" \
	"xymondxboard did not declare the charset from xymond's HTMLCONTENTTYPE:
${board%%$'\n'*}"

# feed CHARSET-DIRECTIVE -- run appfeed with that HTMLCONTENTTYPE, answer in OUT
feed() {
	printf 'XYMONDPORT="%s"\nXYMSRV="127.0.0.1"\nXYMONSERVERCGIURL="/xymon-cgi"\n%s\n' \
		"$PORT" "$1" > "$work/cgi.cfg"
	OUT=$(env REQUEST_METHOD=GET QUERY_STRING="" "$work/appfeed" --env="$work/cgi.cfg" 2>&1)
}

# 2. appfeed names the charset from its own HTMLCONTENTTYPE.
feed 'HTMLCONTENTTYPE="text/html; charset=utf-8"'
assert_contains "<?xml version='1.0' encoding='utf-8'?>" "$OUT" \
	"appfeed did not declare the configured utf-8 charset:
${OUT%%$'\n'*}"

# 3. No charset in HTMLCONTENTTYPE falls back to utf-8, not the old literal.
feed 'HTMLCONTENTTYPE="text/html"'
assert_contains "<?xml version='1.0' encoding='utf-8'?>" "$OUT" \
	"appfeed did not fall back to utf-8 when HTMLCONTENTTYPE names no charset:
${OUT%%$'\n'*}"
assert_not_contains "ISO-8859-1" "$OUT" "appfeed still emits the old fixed ISO-8859-1 declaration"

pass "appfeed.cgi and xymondxboard declare the XML encoding from HTMLCONTENTTYPE, not a fixed ISO-8859-1"
