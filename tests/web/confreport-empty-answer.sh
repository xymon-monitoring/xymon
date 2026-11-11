#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/web/confreport-empty-answer.sh
#
# confreport.cgi reads xymond's answer line by line, and stepped on to the
# next line only after a newline. An answer whose last line has none, an
# empty one above all, left it on the same line for ever: the CGI spun at
# full CPU and never answered. A pagepath cookie naming a page that does not
# exist gives such an empty answer, so any visitor could set one off.
#
# Against a real xymond, with the cookie for a page that does not exist, as
# confreport.cgi and as confreport-critical.cgi (--critical, which also read
# the empty line's missing test name and crashed): each must be served (exit
# 0) within 15 seconds, with its "No hosts" page. It is run in the background
# and stopped when that time is up, so a spinning CGI fails the test instead
# of hanging it.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
# shellcheck source=tests/lib/xymond-daemon.sh
. "$(dirname "$0")/../lib/xymond-daemon.sh"

require_bin XYMOND xymond/xymond
require_bin XYMONCLIENT common/xymon
require_bin CONFREPORT web/confreport.cgi
require_shm_segments "$(grep -c 'setup_channel(C_[A-Z_]*, CHAN_MASTER)' "$(find_root)/xymond/xymond.c")"
require_cfg XYMONSERVER_CFG xymond/etcfiles/xymonserver.cfg

work=$(mktempdir)
mkdir -p "$work/home/tmp" "$work/home/web"
printf 'page one One\n127.0.0.1 hosta.example.com # \n' >"$work/hosts.cfg"
sed -e 's|^XYMONHOME=.*|XYMONHOME="'"$work"'/home"|' \
    -e 's|^XYMONTMP=.*|XYMONTMP="'"$work"'/home/tmp"|' \
	"$XYMONSERVER_CFG" >"$work/xymonserver.cfg"
echo "HOSTSCFG=\"$work/hosts.cfg\"" >>"$work/xymonserver.cfg"

XYMOND_PID=""
stop_xymond() {
	[ -n "$XYMOND_PID" ] || return 0
	stop_process "$XYMOND_PID"
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
XYMON_TIMEOUT=5 "$XYMONCLIENT" "127.0.0.1:$PORT" "status hosta,example,com.conn green up" >/dev/null

# report ARGS... -- confreport.cgi in the background, given 15 seconds
report() {
	local cgi rc=0
	env REQUEST_METHOD=GET QUERY_STRING="" HTTP_COOKIE="pagepath=nosuchpage" XYMONHOME="$work/home" \
		"$CONFREPORT" --env="$work/xymonserver.cfg" "$@" >"$work/out" 2>&1 &
	cgi=$!
	register_cleanup "stop_process $cgi"
	for _ in {1..150}; do
		kill -0 "$cgi" 2>/dev/null || break
		sleep 0.1
	done
	if kill -0 "$cgi" 2>/dev/null; then
		stop_process "$cgi"
		fail "confreport.cgi $* did not answer within 15 seconds for a page that does not exist (it spins on an empty answer)"
	fi
	wait "$cgi" || rc=$?
	[ "$rc" -eq 0 ] || fail "confreport.cgi $* for a page that does not exist ended with rc $rc: $(cat "$work/out")"
	assert_contains "No hosts or tests to report" "$(cat "$work/out")" "confreport.cgi $* did not say there is nothing to report"
}
report
report --critical

pass "confreport.cgi, and with --critical, answers for a page that does not exist, on xymond's empty answer"
