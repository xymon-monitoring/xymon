#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/xymond/channel-locator-retry-interval.sh
#
# A network peer that does not answer is retried while it still holds a
# message, every 10 seconds -- well inside --msgtimeout (30 s), which drops a
# message that waits longer. At the old once-a-minute pace, a message queued
# while a worker was down was dropped before the channel tried again.
#
# Runs real daemons: xymond, xymond_locator and the channel. The only rrd
# server is registered by hand with nothing listening on its port, and the
# connect attempts are counted in the channel's log over a fixed window, so
# a slow machine makes more of them, not fewer.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin XYMOND xymond/xymond
require_bin XYMOND_CHANNEL xymond/xymond_channel
require_bin XYMOND_LOCATOR xymond/xymond_locator
require_bin XYMONCMD common/xymoncmd
require_bin XYMON common/xymon
require_bin LOCATOR lib/locator

require_shm_segments "$(grep -c 'setup_channel(C_[A-Z_]*, CHAN_MASTER)' "$(find_root)/xymond/xymond.c")"

work=$(mktempdir)
base=$(( 20000 + (RANDOM % 15000) ))
port=$base lport=$((base + 1)) standby=$((base + 2)) dead=$((base + 3))
mkdir -p "$work"/{home/etc,home/tmp,var,logs,rrd}
cat >"$work/home/etc/hosts.cfg" <<'EOF'
127.0.0.11 hosta # noconn
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

# The only rrd server is one with nothing listening on its port. A negative
# weight makes it the active one, as in channel-locator-failover.sh.
printf '%s\n' "r s 127.0.0.1:$dead rrd -2 0" | "$LOCATOR" "127.0.0.1:$lport" >"$work/register.log" 2>&1
grep -q OK "$work/register.log" || fail "could not register the dead server: $(cat "$work/register.log")"

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

# One status, queued for the dead server. The channel's first connect fails;
# the message stays queued, and the peer is retried while it holds one. The
# message waits up to --msgtimeout (30 s), so in 25 s a 10 s interval makes
# at least two attempts, where the old minute made one.
"$XYMON" "127.0.0.1:$port" "status hosta.cpu green $(date) up: 3 days, 2 users, 120 procs, load=0.50" \
	|| fail "xymond rejected the status"
attempts() { grep -c "Cannot connect to peer 127.0.0.1:$dead" "$work/logs/channel.log" 2>/dev/null || true; }
for _ in {1..50}; do [ "$(attempts)" -ge 1 ] && break; sleep 0.1; done
[ "$(attempts)" -ge 1 ] || fail "the channel never tried the dead server: $(cat "$work/logs/channel.log")"
sleep 25
n=$(attempts)
[ "$n" -ge 2 ] \
	|| fail "the channel tried the unreachable server $n time(s) in 25 s; a network peer holding a message must be retried every 10 s: $(cat "$work/logs/channel.log")"
kill -0 "$chanpid" 2>/dev/null || fail "the channel died: $(cat "$work/logs/channel.log")"

pass "xymond_channel retries an unreachable network peer that holds a message every 10 seconds ($n attempts in 25 s)"
