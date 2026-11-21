#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/web/criticalview-ack-escape.sh
#
# The Critical Systems view puts an acknowledged status's message in its
# icon's TITLE attribute. The message is whatever the acknowledger typed, so it
# has to be escaped, or a '"' in it ends the attribute and what follows is
# markup. With a real xymond holding an acknowledged critical status, checked
# that a message carrying '"' comes out as text, and an ordinary message
# still shows.

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
"$CC" $cflags -o "$work/criticalview" "$ROOT/web/criticalview.c" \
	"$(xymon_lib "$ROOT" libxymon.a)" "$(xymon_lib "$ROOT" libxymoncomm.a)" "$(xymon_lib "$ROOT" libxymon.a)" \
	$pcre_libs $ldflags 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; fail "criticalview does not compile"; }

mkdir -p "$work/etc" "$work/home/etc" "$work/home/tmp"
{
	printf '127.0.0.1 web01.example.com # \n'
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
printf 'XYMONDPORT="%s"\nXYMSRV="127.0.0.1"\nXYMONSERVERCGIURL="/xymon-cgi"\nHTMLCONTENTTYPE="text/html"\nXYMONHOME="%s/home"\nHOSTSCFG="!%s/etc/hosts.cfg"\n' \
	"$PORT" "$work" "$work" > "$work/cgi.cfg"
printf 'web01.example.com|conn||||1|||\n' > "$work/critical.cfg"

x() { XYMON_TIMEOUT=5 "$XYMONCLIENT" "127.0.0.1:$PORT" "$1"; }

x "status web01,example,com.conn red Host not responding" >/dev/null
# ackinfo HOST.TEST / level / seconds valid / acked by / message: the
# critical view's own acknowledgement, as ackinfo.cgi sends it
x "$(printf 'ackinfo web01.example.com.conn\n1\n3600\noperator <b>x</b>\nwatch "db02" closely')" >/dev/null
for _ in $(seq 1 50); do
	got=$(x 'xymondboard fields=hostname,testname,acklist' | grep '^web01.example.com|conn|' || true)
	case $got in *db02*) break ;; esac
	sleep 0.1
done
case $got in *db02*) ;; *) fail "xymond did not take the acknowledgement: '$got'" ;; esac

set +e
OUT=$(env REQUEST_METHOD=GET QUERY_STRING="WANTACKED=yes" "$work/criticalview" \
	--env="$work/cgi.cfg" --config="$work/critical.cfg" 2>&1)
RC=$?
set -e
[ "$RC" -eq 0 ] || fail "criticalview failed (exit $RC): $OUT"

assert_contains "db02" "$OUT" "the acknowledged status is not on the view, so its message was not shown"
assert_not_contains 'watch "db02" closely' "$OUT" "an acknowledgement message ended its TITLE attribute"
assert_contains 'watch &quot;db02&quot; closely' "$OUT" "an acknowledgement message was not shown escaped"

pass "the Critical Systems view escapes an acknowledgement message in its TITLE"
