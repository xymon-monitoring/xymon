#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/xymond/channel-local-respawn.sh
#
# A local worker that fails on every message is started again at most once a
# minute, as it always was: network peers are retried every 10 seconds, but a
# local one restarted at that pace would be respawned, and log its failure,
# six times as often.
#
# Runs a real xymond and a channel whose worker records each start, then
# exits at once. Statuses keep coming for 25 seconds, and the worker must have
# started exactly once in that window.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin XYMOND xymond/xymond
require_bin XYMOND_CHANNEL xymond/xymond_channel
require_bin XYMON common/xymon

require_shm_segments "$(grep -c 'setup_channel(C_[A-Z_]*, CHAN_MASTER)' "$(find_root)/xymond/xymond.c")"

work=$(mktempdir)
port=$(( 20000 + (RANDOM % 15000) ))
mkdir -p "$work"/{home/etc,home/tmp,var,logs}
cat >"$work/home/etc/hosts.cfg" <<'HOSTS'
127.0.0.11 hosta # noconn
HOSTS
cat >"$work/xymonserver.cfg" <<CFG
XYMONHOME="$work/home"
XYMONVAR="$work/var"
XYMONSERVERLOGS="$work/logs"
XYMONTMP="$work/home/tmp"
HOSTSCFG="$work/home/etc/hosts.cfg"
XYMONSERVERHOSTNAME="hosta"
XYMONSERVERIP="127.0.0.1"
XYMSRV="127.0.0.1"
XYMONDPORT="$port"
CFG

# The worker: note the start, then fail without reading anything.
cat >"$work/worker.sh" <<WORKER
#!/bin/sh
echo start >>"$work/starts"
exit 1
WORKER
chmod +x "$work/worker.sh"

stop_daemons() {
	"$XYMON" "127.0.0.1:$port" shutdown >/dev/null 2>&1 || true
	for pidfile in "$work/channel.pid" "$work/xymond.pid"; do
		[ -f "$pidfile" ] && kill "$(cat "$pidfile")" 2>/dev/null || true
	done
}
register_cleanup stop_daemons

"$XYMOND" --env="$work/xymonserver.cfg" --hosts="$work/home/etc/hosts.cfg" \
	--listen="127.0.0.1:$port" --pidfile="$work/xymond.pid" --daemon \
	--log="$work/logs/xymond.log"
for _ in {1..100}; do
	"$XYMON" "127.0.0.1:$port" ping >/dev/null 2>&1 && break
	sleep 0.1
done

"$XYMOND_CHANNEL" --env="$work/xymonserver.cfg" --channel=status \
	--pidfile="$work/channel.pid" --daemon --log="$work/logs/channel.log" "$work/worker.sh"
for _ in {1..50}; do [ -s "$work/channel.pid" ] && break; sleep 0.1; done
chanpid=$(cat "$work/channel.pid")

starts() { if [ -f "$work/starts" ]; then grep -c start "$work/starts"; else echo 0; fi; }
status() {
	"$XYMON" "127.0.0.1:$port" "status hosta.cpu green $(date) up: 3 days, 2 users, 120 procs, load=0.50" \
		|| fail "xymond rejected the status"
}

# The first status starts the worker.
status
for _ in {1..50}; do [ "$(starts)" -ge 1 ] && break; sleep 0.1; done
[ "$(starts)" -ge 1 ] || fail "the channel never started its worker: $(cat "$work/logs/channel.log")"

# Then a status a second for 25 s, each one a reason to start it again.
for _ in {1..25}; do status; sleep 1; done
n=$(starts)
[ "$n" -eq 1 ] \
	|| fail "a local worker that fails on every message was started $n times in 25 s, not once: it must wait a minute between starts"
kill -0 "$chanpid" 2>/dev/null || fail "the channel died: $(cat "$work/logs/channel.log")"

pass "xymond_channel starts a failing local worker once in 25 s: it waits a minute between starts"
