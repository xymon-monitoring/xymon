#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/xymonproxy-report-fd.sh
#
# With --report, xymonproxy sends a status about itself, from a connection
# record it builds with calloc(). A socket field left at calloc()'s 0 rather
# than -1 is taken for an open descriptor when that record is cleaned up:
# the proxy closed descriptor 0 after every report, and counted one socket
# fewer than it had. Descriptor 0 is the proxy's standard input, or, once
# closed, whatever connection reuses it next.
#
# The proxy's standard input is a FIFO this test holds the other end of.
# Once the first report -- sent at start -- has reached the xymond and been
# cleaned up, writing to the FIFO must still succeed: it fails with no reader
# left, which is what the proxy closing descriptor 0 leaves.

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

# The proxy's standard input. Its open for reading waits for a writer, and
# ours for writing waits for a reader: the proxy is started first, in the
# background, and the test's end opened after it.
mkfifo "$work/stdin"
PPORT=$(free_port)
: >"$work/proxy.log"
"$XYMONPROXY" --no-daemon --listen="127.0.0.1:$PPORT" --server="127.0.0.1:$XPORT" \
	--report=relayhost.proxy <"$work/stdin" >"$work/proxy.log" 2>&1 &
proxy_pid=$!
PIDS="$PIDS $proxy_pid"
exec 7>"$work/stdin"
register_cleanup 'exec 7>&-'

# The first report is sent at start; wait for it on the xymond's board
got=
for _ in {1..100}; do
	got=$(XYMON_TIMEOUT=5 "$XYMONCLIENT" "127.0.0.1:$XPORT" "xymondboard host=relayhost test=proxy fields=color" 2>/dev/null || true)
	[ -n "$got" ] && break
	kill -0 "$proxy_pid" 2>/dev/null || fail "xymonproxy stopped: $(cat "$work/proxy.log")"
	sleep 0.1
done
[ -n "$got" ] || fail "xymonproxy's report did not reach the xymond: $(cat "$work/proxy.log")"
# The report's record is cleaned up in the same pass that delivered it
sleep 1
kill -0 "$proxy_pid" 2>/dev/null || fail "xymonproxy stopped after its report: $(cat "$work/proxy.log")"

# A write to the FIFO fails once no reader is left (EPIPE, SIGPIPE ignored)
( trap '' PIPE; printf 'x\n' >&7 ) 2>/dev/null \
	|| fail "xymonproxy closed its standard input after sending its report: a record's socket field was taken for descriptor 0"

pass "xymonproxy sends its report and keeps its standard input, descriptor 0, open"
