#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/channel-forged-drophost.sh
#
# xymond_history obeys @@drophost records, which xymond writes when an
# administrator drops a host, removing that host's whole history. Because a
# message line starting with "@@" is now passed on starting with "@ ", a
# "@@drophost" line inside one host's status no longer reaches xymond_history
# as a drop of another host: that host's history stays.
#
# victim gets statuses first, so xymond_history writes all three parts of its
# history -- the status event file, the host event log, and the detailed
# history logs under XYMONHISTLOGS -- and each is checked afterwards; then
# testhost sends the status carrying the @@drophost line.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
# shellcheck source=tests/lib/xymond-daemon.sh
. "$(dirname "$0")/../lib/xymond-daemon.sh"

require_bin XYMOND xymond/xymond
require_bin XYMOND_CHANNEL xymond/xymond_channel
require_bin XYMOND_HISTORY xymond/xymond_history
require_bin XYMON common/xymon
XYMONCLIENT=$XYMON
require_shm_segments "$(grep -c 'setup_channel(C_[A-Z_]*, CHAN_MASTER)' "$(find_root)/xymond/xymond.c")"

work=$(mktempdir)
mkdir -p "$work"/{home/etc,home/tmp,var,logs,hist,histlogs}
printf '127.0.0.1 testhost # noconn\n127.0.0.2 victim # noconn\n' >"$work/home/etc/hosts.cfg"

xymond_launch() {
	local port=$1; shift
	cat >"$work/xymonserver.cfg" <<EOF
XYMONHOME="$work/home"
XYMONVAR="$work/var"
XYMONSERVERLOGS="$work/logs"
XYMONTMP="$work/home/tmp"
XYMONHISTDIR="$work/hist"
XYMONHISTLOGS="$work/histlogs"
HOSTSCFG="!$work/home/etc/hosts.cfg"
XYMONSERVERHOSTNAME="testhost"
XYMONSERVERIP="127.0.0.1"
MACHINEDOTS="testhost"
XYMONDPORT="$port"
EOF
	"$XYMOND" --env="$work/xymonserver.cfg" --hosts="$work/home/etc/hosts.cfg" \
		--listen="127.0.0.1:$port" --pidfile="$work/xymond.pid" --no-daemon "$@" \
		>"$work/xymond.log" 2>&1 &
	XYMOND_PID=$!
}

stop_daemons() {
	"$XYMON" "127.0.0.1:${PORT:-}" shutdown >/dev/null 2>&1 || true
	for pidfile in "$work/channel.pid" "$work/xymond.pid"; do
		if [ -f "$pidfile" ]; then
			stop_process "$(cat "$pidfile")"
		fi
	done
}
register_cleanup stop_daemons

start_xymond

"$XYMOND_CHANNEL" --env="$work/xymonserver.cfg" --channel=stachg \
	--pidfile="$work/channel.pid" --daemon --log="$work/logs/history.log" \
	"$XYMOND_HISTORY" --env="$work/xymonserver.cfg" \
	--histdir="$work/hist" --histlogdir="$work/histlogs"

# victim's history: wait until xymond_history has written it. A status sent
# before the worker is attached is not delivered, so it is sent again.
for _ in $(seq 1 100); do
	"$XYMON" "127.0.0.1:$PORT" "status victim.cpu red load high" >/dev/null 2>&1 || true
	[ -s "$work/hist/victim.cpu" ] && [ -n "$(ls "$work/histlogs/victim/cpu" 2>/dev/null)" ] && break
	sleep 0.1
	"$XYMON" "127.0.0.1:$PORT" "status victim.cpu green load ok" >/dev/null 2>&1 || true
	[ -s "$work/hist/victim.cpu" ] && [ -n "$(ls "$work/histlogs/victim/cpu" 2>/dev/null)" ] && break
	sleep 0.1
done
[ -s "$work/hist/victim.cpu" ] || fail "xymond_history wrote no history for victim: $(cat "$work/logs/history.log" 2>/dev/null)"
[ -n "$(ls "$work/histlogs/victim/cpu" 2>/dev/null)" ] || fail "xymond_history wrote no history log for victim: $(cat "$work/logs/history.log" 2>/dev/null)"

# The @@drophost line, in a status from testhost; then a marker status, so
# any later record has been handled before the check
"$XYMON" "127.0.0.1:$PORT" "$(printf 'status testhost.forgery red before\n@@\n@@drophost|0|127.0.0.1|victim\nafter')"
for _ in $(seq 1 100); do
	"$XYMON" "127.0.0.1:$PORT" "status testhost.marker red done" >/dev/null 2>&1 || true
	[ -s "$work/hist/testhost.marker" ] && break
	sleep 0.1
done
[ -s "$work/hist/testhost.marker" ] || fail "xymond_history did not handle the statuses after the first one: $(cat "$work/logs/history.log" 2>/dev/null)"

assert_file_exists "$work/hist/victim.cpu" "victim's status history survives a "@@drophost" line in another host's status"
assert_file_exists "$work/hist/victim" "victim's host event log survives a "@@drophost" line in another host's status"
[ -n "$(ls "$work/histlogs/victim/cpu" 2>/dev/null)" ] || fail "victim's history logs did not survive a "@@drophost" line in another host's status"

pass "a @@drophost line in a status text does not drop another host's history"
