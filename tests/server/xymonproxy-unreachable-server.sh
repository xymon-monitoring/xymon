#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/xymonproxy-unreachable-server.sh
#
# When a server refuses xymonproxy's connections, it tries CONNECT_TRIES times,
# CONNECT_INTERVAL (8) seconds apart, then gives the message up for that
# server. It then went on as if the message had been sent: with no server
# socket, it shut down and closed socket -1, and, for a request whose client
# waits for an answer, waited for that answer on socket -1. FD_SET(-1) aborts
# the proxy where it is built with _FORTIFY_SOURCE, as distributions build it;
# where it is not, the client waited until the proxy's --timeout ran out.
#
# Two proxies, side by side, so the test takes the 25 seconds of one give-up:
#
#   - one whose only server is a port nothing listens on, sent "ping", which
#     waits for an answer: once it has given the message up ("Server not
#     responding"), it must still be running, and the ping client must be let
#     go within a few seconds, the request dropped. This, not the abort, shows
#     the fix on a build without _FORTIFY_SOURCE: that proxy has --timeout=60,
#     so a client let go only by the timeout is still waiting then;
#   - one with that port first and a real xymond second, sent a status: the
#     status must still reach the xymond, after the first server is given up.

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
mkdir -p "$work/home/tmp"
printf '127.0.0.1 relayhost # conn\n' >"$work/hosts.cfg"
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

proxy alone "127.0.0.1:$deadport" --timeout=60
alone_port=$PPORT alone_pid=$PPID_
proxy second "127.0.0.1:$deadport,127.0.0.1:$XPORT"
second_port=$PPORT second_pid=$PPID_

# The client waits for the answer that does not come; the proxy gives up first
# Longer than the proxy's --timeout, so the proxy is what lets it go
XYMON_TIMEOUT=120 "$XYMONCLIENT" "127.0.0.1:$alone_port" ping >/dev/null 2>&1 &
ping_pid=$!
PIDS="$PIDS $ping_pid"
XYMON_TIMEOUT=5 "$XYMONCLIENT" "127.0.0.1:$second_port" "status relayhost.relay green relayed" >/dev/null 2>&1 \
	|| fail "the second proxy refused the status: $(cat "$work/second.log")"

got=
for _ in {1..400}; do
	got=$(XYMON_TIMEOUT=5 "$XYMONCLIENT" "127.0.0.1:$XPORT" "xymondboard host=relayhost test=relay fields=color" 2>/dev/null || true)
	[ "$got" = green ] && grep -q 'Server not responding' "$work/alone.log" 2>/dev/null && break
	kill -0 "$alone_pid" 2>/dev/null || break
	sleep 0.1
done
assert_contains "Server not responding" "$(cat "$work/alone.log")" "xymonproxy did not give the message up"
sleep 1
kill -0 "$alone_pid" 2>/dev/null || fail "xymonproxy stopped after giving up on its server: $(cat "$work/alone.log")"
# Dropped: the proxy closes the client's connection, and the client ends
for _ in {1..50}; do
	kill -0 "$ping_pid" 2>/dev/null || break
	sleep 0.1
done
kill -0 "$ping_pid" 2>/dev/null && fail "the ping client is still waiting after the proxy gave its request up"
[ "$got" = green ] || fail "the status did not reach the second server after the first was given up: board '$got', proxy: $(cat "$work/second.log")"
kill -0 "$second_pid" 2>/dev/null || fail "the second proxy stopped: $(cat "$work/second.log")"

pass "xymonproxy gives up a message for a server it cannot reach, goes on to the next, and keeps running"
