#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/xymond/channel-locator-broadcast.sh
#
# A xymond_channel feeding two network workers through xymond_locator must
# survive a broadcast message.
#
# xymond posts some messages to every reader of a channel -- "logrotate" on
# every HUP, "reload", "shutdown" -- and xymond_channel queues such a message
# on each of its peers. It queued the same buffer on all of them, and every
# peer frees the message it holds once it is sent: with two peers the buffer
# was freed twice, and glibc aborted the channel ("free(): double free
# detected"). A local worker is one peer, so this took a locator set-up with
# two workers -- and then every log rotation. Each peer now gets its own copy.
#
# Runs real daemons: xymond, xymond_locator, two xymond_rrd network workers,
# and the channel between them; sends statuses until both workers hold a
# host, then HUPs xymond and checks the channel is still there and still
# delivers.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin XYMOND xymond/xymond
require_bin XYMOND_CHANNEL xymond/xymond_channel
require_bin XYMOND_LOCATOR xymond/xymond_locator
require_bin XYMOND_RRD xymond/xymond_rrd
require_bin XYMONCMD common/xymoncmd
require_bin XYMON common/xymon
require_bin LOCATOR lib/locator

require_shm_segments "$(grep -c 'setup_channel(C_[A-Z_]*, CHAN_MASTER)' "$(find_root)/xymond/xymond.c")"

work=$(mktempdir)
base=$(( 20000 + (RANDOM % 15000) ))
port=$base lport=$((base + 1)) p1=$((base + 2)) p2=$((base + 3))
mkdir -p "$work"/{home/etc,home/tmp,var,logs,rrd1,rrd2}
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

for n in 1 2; do
	eval "wport=\$p$n"
	"$XYMONCMD" --env="$work/xymonserver.cfg" "$XYMOND_RRD" --rrddir="$work/rrd$n" \
		--listen="127.0.0.1:$wport" --locator="127.0.0.1:$lport" --locatorweight=2 \
		>"$work/logs/rrd$n.log" 2>&1 &
	pids="$pids $!"
done

# --log: xymond posts "logrotate" on a HUP only when it has a log to rotate.
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

# Until both workers hold a host: the locator spreads new hosts between them,
# and the channel opens a connection to each on its first message.
for h in hosta hostb hostc hostd; do status "$h"; done
both=
for _ in {1..100}; do
	if grep -q host <<<"$(ls "$work/rrd1")" && grep -q host <<<"$(ls "$work/rrd2")"; then both=1; break; fi
	sleep 0.1
done
[ -n "$both" ] || fail "the statuses did not reach both workers: rrd1=$(ls "$work/rrd1") rrd2=$(ls "$work/rrd2") $(cat "$work/logs/channel.log")"

# HUP: xymond posts "logrotate" to every channel reader, a broadcast.
kill -HUP "$(cat "$work/xymond.pid")"
sleep 2
assert_not_contains "double free" "$(cat "$work/logs/channel.log")" \
	"the channel freed a broadcast message twice"
kill -0 "$chanpid" 2>/dev/null \
	|| fail "the channel died on a broadcast message to two workers: $(cat "$work/logs/channel.log")"

# Still delivering: a host not seen before the HUP reaches a worker.
status hoste
got=
for _ in {1..100}; do
	if grep -q hoste <<<"$(ls "$work/rrd1" "$work/rrd2")"; then got=1; break; fi
	sleep 0.1
done
[ -n "$got" ] || fail "after the broadcast the channel no longer delivered: $(cat "$work/logs/channel.log")"

pass "xymond_channel survives a broadcast to two locator workers, and keeps delivering"
