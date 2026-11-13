#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/xymond/channel-local-respawn.sh
#
# A local worker that fails on every message is started again at most every
# --respawn-interval seconds, once a minute by default, as it always was:
# network peers are retried every 10 seconds, but a local one restarted at
# that pace would be respawned, and log its failure, six times as often.
#
# Runs a real xymond and a channel whose worker records each start, then
# exits at once. With the interval at 3 s and statuses arriving all the
# while, the worker must not start again within a second of its first start,
# and must start again within 6 s -- which neither the 60 s default nor the
# 10 s network pace could do, so the option is what paced it. The 60 s and
# 10 s defaults themselves are read from the line the channel logs at
# startup.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
# shellcheck source=tests/lib/xymond-daemon.sh
. "$(dirname "$0")/../lib/xymond-daemon.sh"

require_bin XYMOND xymond/xymond
require_bin XYMOND_CHANNEL xymond/xymond_channel
require_bin XYMON common/xymon
XYMONCLIENT="$XYMON"		# free_port() in lib/xymond-daemon.sh pings with it

require_shm_segments "$(grep -c 'setup_channel(C_[A-Z_]*, CHAN_MASTER)' "$(find_root)/xymond/xymond.c")"

work=$(mktempdir)
# A port drawn from just below the kernel ephemeral range and pinged free;
# free_port() is in lib/xymond-daemon.sh.
port=$(free_port) || fail "no free port"
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
		[ -f "$pidfile" ] && stop_process "$(cat "$pidfile")" || true
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
	--respawn-interval=3 --debug \
	--pidfile="$work/channel.pid" --daemon --log="$work/logs/channel.log" "$work/worker.sh"
for _ in {1..50}; do [ -s "$work/channel.pid" ] && break; sleep 0.1; done
chanpid=$(cat "$work/channel.pid")

starts() { if [ -f "$work/starts" ]; then grep -c start "$work/starts"; else echo 0; fi; }
status() {
	"$XYMON" "127.0.0.1:$port" "status hosta.cpu green $(date) up: 3 days, 2 users, 120 procs, load=0.50" \
		|| fail "xymond rejected the status"
}

# The daemon logs its intervals once its log is open, after the pidfile.
for _ in {1..50}; do grep -q "Network peers are retried every 10 s, a local worker restarted at most every 3 s" "$work/logs/channel.log" 2>/dev/null && break; sleep 0.1; done
grep -q "Network peers are retried every 10 s, a local worker restarted at most every 3 s" "$work/logs/channel.log" \
	|| fail "the channel did not log --respawn-interval=3 beside the 10 s reconnect default: $(cat "$work/logs/channel.log")"

# The first status starts the worker.
status
for _ in {1..50}; do [ "$(starts)" -ge 1 ] && break; sleep 0.1; done
[ "$(starts)" -ge 1 ] || fail "the channel never started its worker: $(cat "$work/logs/channel.log")"

# Then statuses keep coming, each one a reason to start it again. The clock
# counts whole seconds, so the next start comes 2 to 3 s after the first:
# none within the first second.
for _ in {1..4}; do status; sleep 0.25; done
n=$(starts)
[ "$n" -eq 1 ] \
	|| fail "a local worker that fails on every message was started $n times within a second, with --respawn-interval=3"
for _ in {1..24}; do [ "$(starts)" -ge 2 ] && break; status; sleep 0.25; done
n=$(starts)
[ "$n" -ge 2 ] \
	|| fail "the failing local worker was not started again within 6 s, with --respawn-interval=3 and statuses arriving: $(cat "$work/logs/channel.log")"
kill -0 "$chanpid" 2>/dev/null || fail "the channel died: $(cat "$work/logs/channel.log")"

pass "xymond_channel restarts a failing local worker every --respawn-interval seconds and no sooner, once a minute by default"
