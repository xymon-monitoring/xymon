#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/web/xml-encoding-charset.sh
#
# The XML that xymond's xymondxboard/xymondxlog replies and perfdata.cgi emit
# declared "encoding='ISO-8859-1'" with a fixed string, while the status text
# they carry is whatever the hosts send -- UTF-8 by default. A consumer then
# decoded the UTF-8 as Latin-1 and showed mojibake. The declaration now names
# the charset from HTMLCONTENTTYPE, the same the HTML pages use (lib/cgi.c
# htmlcontentcharset()). Declared UTF-8, a byte a host sent that is not part of
# well-formed UTF-8 is written as U+FFFD, so one stray byte cannot make the
# reply unparseable (lib/strfunc.c xmlquoted()/utf8_char()). appfeed shares the
# same writer; its own bytes are pinned in appfeed-charset.sh, so here the
# daemon reply is the subject, with appfeed used only for the declaration.
#
# xymond runs with HTMLCONTENTTYPE charset=UTF-8. A status whose text holds a
# lone Latin-1 byte, an overlong form and U+FFFF, and a well-formed UTF-8 "e
# acute", is posted; its xymondxboard MessageSummary must carry U+FFFD for each
# bad byte, the good character unchanged, and no bad byte raw.

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
sed -e 's|^XYMONHOME=.*|XYMONHOME="'"$work"'/home"|' \
    -e 's|^XYMONTMP=.*|XYMONTMP="'"$work"'/home/tmp"|' \
    -e 's|^HTMLCONTENTTYPE=.*|HTMLCONTENTTYPE="text/html; charset=UTF-8"|' \
	"$XYMONSERVER_CFG" > "$work/xymond.cfg"
grep -q '^HTMLCONTENTTYPE=' "$work/xymond.cfg" \
	|| printf 'HTMLCONTENTTYPE="text/html; charset=UTF-8"\n' >> "$work/xymond.cfg"

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

# The bytes the bad status carries. BAD1 is a lone Latin-1 'e acute' (0xe9, not
# well-formed UTF-8 on its own); OVER is the overlong '/' C0 AF; FFFF is U+FFFF;
# GOOD is a well-formed UTF-8 'e acute' (C3 A9) that must survive.
BAD1=$(printf '\xe9'); OVER=$(printf '\xc0\xaf'); FFFF=$(printf '\xef\xbf\xbf')
GOOD=$(printf '\xc3\xa9'); FFFD=$(printf '\xef\xbf\xbd')

# TestFlags is a plain element taken verbatim from the host's status (the
# " <!-- [flags:...] -->" marker, xymond.c handle_status). A host can put & < or
# a stray byte there; in a plain element & < must become entities and a bad byte
# U+FFFD, or the reply is not well-formed. The flags are posted alongside the
# message so both the CDATA (MessageSummary) and the plain (TestFlags) path are
# pinned at once.
FLAGSIN="a&b<c${BAD1}d"; FLAGSOUT="a&amp;b&lt;c${FFFD}d"

x "status web01,example,com.conn red <!-- [flags:${FLAGSIN}] --> mark-${BAD1}-${OVER}-${FFFF}-${GOOD}-end" >/dev/null
for _ in $(seq 1 50); do
	case $(x 'xymondboard host=^web01.example.com$ test=^conn$ fields=color') in
		*red*) break ;;
	esac
	sleep 0.1
done

# 1. The daemon reply declares xymond's own charset (UTF-8 here, not the old literal).
board=$(x "xymondxboard")
assert_contains "<?xml version='1.0' encoding='UTF-8'?>" "$board" \
	"xymondxboard did not declare the charset from xymond's HTMLCONTENTTYPE:
${board%%$'\n'*}"
assert_not_contains "ISO-8859-1" "$board" "xymondxboard still emits the old fixed ISO-8859-1 declaration"

# 2. Declared UTF-8, the MessageSummary keeps the good character and replaces each
#    bad byte with U+FFFD, so no bad byte reaches the reply raw.
assert_contains "$GOOD" "$board" "the well-formed UTF-8 character did not survive in xymondxboard"
assert_contains "$FFFD" "$board" "a bad byte was not replaced with U+FFFD in xymondxboard"
assert_not_contains "$BAD1" "$board" "a lone Latin-1 byte reached xymondxboard raw"
assert_not_contains "$(printf '\xc0')" "$board" "an overlong-form byte reached xymondxboard raw"

# 2b. TestFlags is a plain element from the host's status: & < become entities
#     and the bad byte U+FFFD. Emitted raw -- as it was before this change -- a
#     single & or < would make the whole board not well-formed.
assert_contains "<TestFlags>${FLAGSOUT}</TestFlags>" "$board" \
	"xymondxboard did not escape the host-controlled TestFlags:
$(grep -a 'TestFlags' <<<"$board")"

# feed CHARSET-DIRECTIVE -- run appfeed with that HTMLCONTENTTYPE, answer in OUT
feed() {
	printf 'XYMONDPORT="%s"\nXYMSRV="127.0.0.1"\nXYMONSERVERCGIURL="/xymon-cgi"\n%s\n' \
		"$PORT" "$1" > "$work/cgi.cfg"
	OUT=$(env REQUEST_METHOD=GET QUERY_STRING="" "$work/appfeed" --env="$work/cgi.cfg" 2>&1)
}

# 3. appfeed names the charset from its own HTMLCONTENTTYPE, verbatim.
feed 'HTMLCONTENTTYPE="text/html; charset=utf-8"'
assert_contains "<?xml version='1.0' encoding='utf-8'?>" "$OUT" \
	"appfeed did not declare the configured utf-8 charset:
${OUT%%$'\n'*}"

# 4. No charset falls back to UTF-8, not the old literal.
feed 'HTMLCONTENTTYPE="text/html"'
assert_contains "<?xml version='1.0' encoding='UTF-8'?>" "$OUT" \
	"appfeed did not fall back to UTF-8 when HTMLCONTENTTYPE names no charset:
${OUT%%$'\n'*}"
assert_not_contains "ISO-8859-1" "$OUT" "appfeed still emits the old fixed ISO-8859-1 declaration"

# 5. Declared a single-byte charset, the bytes pass as sent (no UTF-8 filtering).
feed 'HTMLCONTENTTYPE="text/html; charset=ISO-8859-1"'
assert_contains "<?xml version='1.0' encoding='ISO-8859-1'?>" "$OUT" \
	"appfeed did not declare the configured ISO-8859-1 charset"
assert_contains "$BAD1" "$OUT" "under ISO-8859-1 appfeed did not pass the lone Latin-1 byte as sent"

pass "xymondxboard and appfeed declare the XML encoding from HTMLCONTENTTYPE, and a UTF-8 reply replaces each byte that is not well-formed UTF-8 with U+FFFD"
