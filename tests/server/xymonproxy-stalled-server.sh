#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/xymonproxy-stalled-server.sh
#
# A server that takes xymonproxy's connection and then nothing, or that drops
# the connection's packets instead of refusing them, leaves the proxy waiting
# to send until its --timeout runs out. It then dropped the message outright,
# for that server and for every --server after it, where a server that
# refuses the connection is given up alone (xymonproxy-unreachable-server.sh).
# The message must go on to the next server.
#
# The stalled server is a second xymonproxy, stopped with SIGSTOP once it
# listens: the kernel still completes connections to it, and nothing reads
# them. A status larger than the socket buffers on both sides then cannot be
# written to the end, which is the wait that times out. Sent through a proxy
# with that server first and a real xymond second, and --timeout=3, the
# status must reach the xymond, whole enough to be on its board.
#
# Not checked: a server that drops the connect itself, which ends in the same
# branch, and the count in the proxy's own status report, sent every five
# minutes.

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

# The server that stalls: it listens, and is stopped
proxy stalled "127.0.0.1:$XPORT"
stalled_port=$PPORT
stalled_pid=$PPID_
kill -STOP "$stalled_pid" || fail "the stalled server could not be stopped"

proxy relay "127.0.0.1:$stalled_port,127.0.0.1:$XPORT" --timeout=3
relay_port=$PPORT relay_pid=$PPID_

# 16 MB: more than a connection nobody reads can take in its buffers
awk 'BEGIN {
	print "status relayhost.relay green relayed"
	for (i = 0; i < 80; i++) line = line "x"
	for (i = 0; i < 200000; i++) print line
}' >"$work/status.txt"
XYMON_TIMEOUT=30 "$XYMONCLIENT" "127.0.0.1:$relay_port" "@" <"$work/status.txt" >/dev/null 2>&1 \
	|| fail "the proxy refused the status: $(cat "$work/relay.log")"

got=
for _ in {1..300}; do
	got=$(XYMON_TIMEOUT=5 "$XYMONCLIENT" "127.0.0.1:$XPORT" "xymondboard host=relayhost test=relay fields=color" 2>/dev/null || true)
	[ "$got" = green ] && break
	kill -0 "$relay_pid" 2>/dev/null || break
	sleep 0.1
done
# Woken before anything fails, so that the cleanup's TERM ends it at once
kill -CONT "$stalled_pid" 2>/dev/null || true
kill -0 "$relay_pid" 2>/dev/null || fail "xymonproxy stopped: $(cat "$work/relay.log")"
[ "$got" = green ] \
	|| fail "the status did not reach the second server after the first stalled past --timeout: board '$got', proxy: $(cat "$work/relay.log")"

pass "xymonproxy gives up a message for a server that stalls past --timeout, and goes on to the next"
