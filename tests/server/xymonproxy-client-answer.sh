#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/xymonproxy-client-answer.sh
#
# A "client" message goes through xymonproxy to every --server, and the
# client gets one answer back: its host's client-local.cfg section, which
# xymonclient.sh keeps as its logfetch configuration. The answer was read from
# the last server only. With that one unreachable, the client got none, though
# an earlier server had taken the message and could have answered: a change to
# client-local.cfg then did not reach clients behind the proxy until that last
# server came back. The answer must come from the last server reached.
#
# A proxy with a real xymond first and a port nothing listens on last, sent a
# client message for a host client-local.cfg has a section for: once the proxy
# has given the last server up (CONNECT_TRIES connects, CONNECT_INTERVAL apart,
# about 25 seconds), the client must get the xymond's section. A proxy with
# the xymond alone gives the same answer at once, which shows the section is
# what the xymond sends.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
# shellcheck source=tests/lib/xymond-daemon.sh
. "$(dirname "$0")/../lib/xymond-daemon.sh"

require_bin XYMOND xymond/xymond
require_bin XYMONPROXY xymonproxy/xymonproxy
require_bin XYMONCLIENT common/xymon
require_shm_segments "$(grep -c 'setup_channel(C_[A-Z_]*, CHAN_MASTER)' "$(find_root)/xymond/xymond.c")"
require_cfg XYMONSERVER_CFG xymond/etcfiles/xymonserver.cfg

work=$(mktempdir)
mkdir -p "$work/home/tmp" "$work/home/etc"
printf '127.0.0.1 relayhost # conn\n' >"$work/hosts.cfg"
printf '[relayhost]\nlog:/var/log/answered-by-xymond:10240\n' >"$work/home/etc/client-local.cfg"
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
	local port=$1
	shift
	"$XYMOND" --no-daemon --listen="127.0.0.1:$port" \
		--hosts="$work/hosts.cfg" --env="$work/xymonserver.cfg" \
		--pidfile="$work/xymond.pid" --checkpoint-file="$work/chk.out" \
		"$@" >"$work/xymond.log" 2>&1 &
	XYMOND_PID=$!
}
start_xymond
XPORT=$PORT
deadport=$(free_port)

PIDS=""
# shellcheck disable=SC2016  # $PIDS is read when the cleanup runs
register_cleanup 'stop_process $PIDS'

# proxy NAME SERVERS [ARGS...] -- a xymonproxy for SERVERS on a free port,
# logging to NAME.log; its port in PPORT and its pid in PPID_. Another port
# when the one picked could not be bound.
proxy() {
	local name=$1 servers=$2
	shift 2
	for _ in 1 2 3 4 5; do
		PPORT=$(free_port)
		[ "$PPORT" != "$deadport" ] || continue
		# Emptied in the parent: the redirection below empties it in the
		# background child, after the wait may already have read it.
		: >"$work/$name.log"
		"$XYMONPROXY" --no-daemon --listen="127.0.0.1:$PPORT" --server="$servers" "$@" >"$work/$name.log" 2>&1 &
		PPID_=$!
		PIDS="$PIDS $PPID_"
		for _ in {1..50}; do
			grep -q 'Sending to Xymon' "$work/$name.log" 2>/dev/null && return 0
			kill -0 "$PPID_" 2>/dev/null || break
			sleep 0.1
		done
		stop_process "$PPID_"
		grep -q 'Cannot bind' "$work/$name.log" 2>/dev/null || break
	done
	fail "xymonproxy did not start: $(cat "$work/$name.log")"
}

msg='client relayhost.linux linux
[date]
Wed Nov 25 12:00:00 UTC 2026'

proxy alone "127.0.0.1:$XPORT"
got=$(XYMON_TIMEOUT=10 "$XYMONCLIENT" "127.0.0.1:$PPORT" "$msg" 2>&1) || true
assert_contains "answered-by-xymond" "$got" \
	"the xymond alone did not answer the client message with its client-local.cfg section, so the case below proves nothing"

proxy lastdead "127.0.0.1:$XPORT,127.0.0.1:$deadport" --timeout=60
# Longer than the proxy's give-up on the last server, about 25 seconds
got=$(XYMON_TIMEOUT=90 "$XYMONCLIENT" "127.0.0.1:$PPORT" "$msg" 2>&1) || true
assert_contains "Server not responding" "$(cat "$work/lastdead.log")" \
	"xymonproxy did not give the last server up"
assert_contains "answered-by-xymond" "$got" \
	"with the last server unreachable, the client did not get the answer of the server reached before it: '$got'"

pass "xymonproxy answers a client message from the last server it reached when the last one listed cannot be reached"
