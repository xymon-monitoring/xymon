#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/xymond/channel-locator-reconnect.sh
#
# A xymond_channel feeding a network worker through xymond_locator must
# deliver the messages sent while that worker restarts.
#
# A restarted worker leaves the channel holding a connection to the process
# that exited. The first write into it succeeded and was lost without an
# error; the next failed, and from then on the channel kept only the newest
# message for that peer, retried the connection once a minute -- longer than
# the 30 seconds --msgtimeout lets a message wait -- reported the server down
# to the locator, and never reported it up again. The channel now sees the
# closed connection before writing, keeps the queue (rewinding a message the
# old connection took only part of), retries every 10 seconds, also retries a
# failed peer that still has messages queued, and tells the locator the server
# is up once it answers.
#
# Runs real daemons: xymond, xymond_locator, one xymond_rrd network worker and
# the channel; restarts the worker, then sends statuses for two hosts the
# channel has not seen, and checks both reach the worker in well under the
# message timeout.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin XYMOND xymond/xymond
require_bin XYMOND_CHANNEL xymond/xymond_channel
require_bin XYMOND_LOCATOR xymond/xymond_locator
require_bin XYMOND_RRD xymond/xymond_rrd
require_bin XYMONCMD common/xymoncmd
require_bin XYMON common/xymon
# ps finds the children the worker's listener forked, which hold the channel's
# connection; a minimal container may not have it.
command -v ps >/dev/null 2>&1 \
	|| skip "ps not available (needed to stop the worker's connection children)"
require_bin LOCATOR lib/locator

require_shm_segments "$(grep -c 'setup_channel(C_[A-Z_]*, CHAN_MASTER)' "$(find_root)/xymond/xymond.c")"

work=$(mktempdir)
base=$(( 20000 + (RANDOM % 15000) ))
port=$base lport=$((base + 1)) p1=$((base + 2))
mkdir -p "$work"/{home/etc,home/tmp,var,logs,rrd1}
cat >"$work/home/etc/hosts.cfg" <<'EOF'
127.0.0.11 hosta # noconn
127.0.0.12 hostb # noconn
127.0.0.13 hostc # noconn
127.0.0.14 hostd # noconn
127.0.0.15 hoste # noconn
EOF
cat >"$work/xymonserver.cfg" <<EOF
XYMONHOME="$work/home"
XYMONVAR="$work/var"
XYMONSERVERLOGS="$work/logs"
XYMONTMP="$work/home/tmp"
HOSTSCFG="$work/home/etc/hosts.cfg"
XYMONSERVERHOSTNAME="hosta"
XYMONSERVERIP="127.0.0.1"
XYMSRV="127.0.0.1"
XYMONDPORT="$port"
EOF

pids=""
stop_daemons() {
	"$XYMON" "127.0.0.1:$port" shutdown >/dev/null 2>&1 || true
	for p in $pids; do kill "$p" 2>/dev/null || true; done
	for pidfile in "$work/channel.pid" "$work/xymond.pid"; do
		[ -f "$pidfile" ] && kill "$(cat "$pidfile")" 2>/dev/null || true
	done
}
register_cleanup stop_daemons

"$XYMOND_LOCATOR" --listen="127.0.0.1:$lport" --logfile="$work/logs/locator.log" &
pids="$pids $!"
for _ in {1..50}; do
	grep -q "Locator is available" <<<"$("$LOCATOR" "127.0.0.1:$lport" </dev/null 2>/dev/null)" && break
	sleep 0.1
done

# start_worker -- the one RRD worker, in the background; sets WPID.
start_worker() {
	"$XYMONCMD" --env="$work/xymonserver.cfg" "$XYMOND_RRD" --rrddir="$work/rrd1" \
		--listen="127.0.0.1:$p1" --locator="127.0.0.1:$lport" --locatorweight=2 \
		>>"$work/logs/rrd1.log" 2>&1 &
	WPID=$!
	pids="$pids $WPID"
}
start_worker
# The worker registers with the locator just before it starts listening, and
# says so; a status sent before that finds no rrd server and is dropped.
for _ in {1..100}; do
	grep -q 'Setting up network listener' "$work/logs/rrd1.log" 2>/dev/null && break
	sleep 0.1
done
grep -q 'Setting up network listener' "$work/logs/rrd1.log" 2>/dev/null \
	|| fail "the rrd worker never registered with the locator: $(cat "$work/logs/rrd1.log" 2>/dev/null)"

# --log keeps xymond's own output with the other logs, for a failure message.
"$XYMOND" --env="$work/xymonserver.cfg" --hosts="$work/home/etc/hosts.cfg" \
	--listen="127.0.0.1:$port" --pidfile="$work/xymond.pid" --daemon \
	--log="$work/logs/xymond.log"
for _ in {1..100}; do
	"$XYMON" "127.0.0.1:$port" ping >/dev/null 2>&1 && break
	sleep 0.1
done

"$XYMOND_CHANNEL" --env="$work/xymonserver.cfg" --channel=status \
	--locator="127.0.0.1:$lport" --service=rrd \
	--pidfile="$work/channel.pid" --daemon --log="$work/logs/channel.log"
for _ in {1..50}; do [ -s "$work/channel.pid" ] && break; sleep 0.1; done
chanpid=$(cat "$work/channel.pid")

# status HOST -- one cpu status, which xymond_rrd turns into RRD files.
status() {
	"$XYMON" "127.0.0.1:$port" "status $1.cpu green $(date) up: 3 days, 2 users, 120 procs, load=0.50" \
		|| fail "xymond rejected the status for $1"
}

# The connection the restart will leave dead: one status, delivered.
status hosta
for _ in {1..100}; do grep -q hosta <<<"$(ls "$work/rrd1")" && break; sleep 0.1; done
grep -q hosta <<<"$(ls "$work/rrd1")" || fail "the first status did not reach the worker: $(cat "$work/logs/channel.log")"

# Restart the worker on the same port, and wait until it listens again.
# Its listener forks a child per connection, and that child holds the
# channel's connection: stop it as well, as stopping the service does, or the
# old connection keeps working and nothing is tested.
kids=$(ps -A -o pid= -o ppid= | awk -v p="$WPID" '$2 == p { print $1 }')
kill "$WPID" $kids 2>/dev/null || true
for _ in {1..100}; do
	alive=
	for p in "$WPID" $kids; do kill -0 "$p" 2>/dev/null && alive=1; done
	[ -n "$alive" ] || break
	sleep 0.1
done
[ -z "$alive" ] || fail "the worker did not stop"
start_worker
for _ in {1..100}; do
	[ "$(grep -c 'Setting up network listener' "$work/logs/rrd1.log")" -ge 2 ] && break
	sleep 0.1
done
sleep 1

# Two hosts the channel has not seen: the first goes into the dead connection.
status hostb
status hostc
got=
for _ in {1..250}; do
	if grep -q hostb <<<"$(ls "$work/rrd1")" && grep -q hostc <<<"$(ls "$work/rrd1")"; then got=1; break; fi
	sleep 0.1
done
[ -n "$got" ] || fail "statuses sent after the worker restarted did not all arrive within 25s: rrd1=$(ls "$work/rrd1" | tr '\n' ' ') -- $(cat "$work/logs/channel.log")"
kill -0 "$chanpid" 2>/dev/null || fail "the channel died: $(cat "$work/logs/channel.log")"

pass "xymond_channel delivers the messages sent while a locator worker restarts"
