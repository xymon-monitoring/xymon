#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/xymond/channel-locator-failover.sh
#
# A xymond_channel must fail over to a standby worker when the locator names
# a server that does not answer.
#
# The locator learns that a server is down from the channel. The channel told
# it only when a write on an open connection failed; a connect that failed
# was logged and nothing more. So failover worked only when the active worker
# died while the channel held a connection to it. A locator restarted while
# the active worker was down reloads that worker as up from its state file,
# the channel's connects to it are refused, and every message went to the
# dead server for as long as it stayed down -- for alerts, no alert at all.
# The channel now reports the server down when the connect fails too.
#
# Runs real daemons: xymond, xymond_locator, one xymond_rrd network worker on
# standby and the channel. The active server is registered by hand, with
# nothing listening on its port: what the locator holds after such a restart.
# Statuses are then sent until one reaches the standby.

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

# Negative weights make one server active and the others standby; the lowest
# weight is the active one. The standby is the real worker.
"$XYMONCMD" --env="$work/xymonserver.cfg" "$XYMOND_RRD" --rrddir="$work/rrd" \
	--listen="127.0.0.1:$standby" --locator="127.0.0.1:$lport" --locatorweight=-1 \
	>>"$work/logs/rrd.log" 2>&1 &
pids="$pids $!"
for _ in {1..100}; do
	grep -q 'Setting up network listener' "$work/logs/rrd.log" 2>/dev/null && break
	sleep 0.1
done
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

# One cpu status a second, which xymond_rrd turns into RRD files, until the
# standby has them. The first goes to the dead server either way; what is
# tested is that the ones after it do not.
got=
for _ in {1..20}; do
	"$XYMON" "127.0.0.1:$port" "status hosta.cpu green $(date) up: 3 days, 2 users, 120 procs, load=0.50" \
		|| fail "xymond rejected the status"
	if [ -d "$work/rrd/hosta" ]; then got=1; break; fi
	sleep 1
done
[ -n "$got" ] || fail "statuses never reached the standby in 20s, so the channel kept sending to a server that refuses connections: $(cat "$work/logs/channel.log")"
kill -0 "$chanpid" 2>/dev/null || fail "the channel died: $(cat "$work/logs/channel.log")"

pass "xymond_channel fails over to the standby when the active server refuses connections"
