#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/web/ghostlist-html.sh
#
# ghostlist.cgi links its column headings and its "Text report" back to
# itself. The parameters of those links are joined with "&amp;", as an HTML
# attribute needs; they had a bare "&" before MAXAGE and TEXT.
#
# The links are printed once xymond answers the ghostlist query, with or
# without ghosts in it, so a xymond with an empty board is enough.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
# shellcheck source=tests/lib/xymond-daemon.sh
. "$(dirname "$0")/../lib/xymond-daemon.sh"

ROOT=$(find_root)
require_bin GHOSTLIST web/ghostlist.cgi
require_bin XYMOND xymond/xymond
require_bin XYMONCLIENT common/xymon
require_shm_segments "$(grep -c 'setup_channel(C_[A-Z_]*, CHAN_MASTER)' "$ROOT/xymond/xymond.c")"
require_cfg XYMONSERVER_CFG xymond/etcfiles/xymonserver.cfg

work=$(mktempdir)
mkdir -p "$work/home/tmp" "$work/home/web"
printf '127.0.0.1 testhost # conn\n' >"$work/hosts.cfg"
sed -e 's|^XYMONHOME=.*|XYMONHOME="'"$work"'/home"|' \
    -e 's|^XYMONTMP=.*|XYMONTMP="'"$work"'/home/tmp"|' \
	"$XYMONSERVER_CFG" >"$work/xymonserver.cfg"

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
		--hosts="$work/hosts.cfg" --env="$work/xymonserver.cfg" \
		--pidfile="$work/xymond.pid" --checkpoint-file="$work/chk.out" \
		"$@" >"$work/xymond.log" 2>&1 &
	XYMOND_PID=$!
}
start_xymond
printf 'XYMSRV="127.0.0.1"\nXYMONDPORT="%s"\n' "$PORT" >>"$work/xymonserver.cfg"

out=$(REQUEST_METHOD=GET QUERY_STRING="SORT=name&MAXAGE=300" XYMONHOME="$work/home" \
	"$GHOSTLIST" --env="$work/xymonserver.cfg" 2>"$work/stderr" || true)

assert_contains 'ghostlist.sh?SORT=sender&amp;MAXAGE=300' "$out" "no column link from the ghost list: $(cat "$work/stderr")"
assert_contains 'ghostlist.sh?SORT=name&amp;MAXAGE=300&amp;TEXT' "$out" "the Text report link is not joined with &amp;"
assert_not_contains '&MAXAGE=' "$out" "a bare & is left before MAXAGE"
assert_not_contains '&TEXT' "$out" "a bare & is left before TEXT"

pass "ghostlist.cgi joins the parameters of its column and Text report links with &amp;"
