#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/xymond/channel-ipv6.sh
#
# A channel reaches a network worker over IPv6 (#116): the worker listens on
# --listen=[::1]:PORT and registers that with the locator, and xymond_channel
# looks it up and connects to it there.
#
# The worker refused an IPv6 --listen, and the channel resolved its peer with
# gethostbyname() and split it at the first ':', so neither end spoke IPv6.
#
# Runs real daemons: xymond, xymond_locator on [::1], one xymond_rrd network
# worker on [::1], and the channel. One status is sent; it has arrived when
# its RRD file appears. The worker's and the channel's logs must name the
# address as [::1]:PORT. A second channel, with a local worker ("cat"), must
# name that worker by its command, as it has no address. Skips where the host
# has no ::1, which is asked of its interface list, so a daemon that fails to
# use ::1 is a failure.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin XYMOND xymond/xymond
# ps finds the children the worker's listener forked; a minimal container may
# not have it.
command -v ps >/dev/null 2>&1 \
	|| skip "ps not available (needed to stop the worker's connection children)"
require_bin XYMOND_CHANNEL xymond/xymond_channel
require_bin XYMOND_LOCATOR xymond/xymond_locator
require_bin XYMOND_RRD xymond/xymond_rrd
require_bin XYMONCMD common/xymoncmd
require_bin XYMON common/xymon
require_bin LOCATOR lib/xymonlocator
require_shm_segments "$(grep -c 'setup_channel(C_[A-Z_]*, CHAN_MASTER)' "$(find_root)/xymond/xymond.c")"
have_ipv6_loopback || skip "this host has no IPv6 loopback (::1)"

work=$(mktempdir)
base=$(( 20000 + (RANDOM % 15000) ))
port=$base lport=$((base + 1)) p1=$((base + 2))
mkdir -p "$work"/{home/etc,home/tmp,var,logs,rrd1}
printf '127.0.0.11 hosta # noconn\n' >"$work/home/etc/hosts.cfg"
{
	printf 'XYMONHOME="%s/home"\nXYMONVAR="%s/var"\n' "$work" "$work"
	printf 'XYMONSERVERLOGS="%s/logs"\nXYMONTMP="%s/home/tmp"\n' "$work" "$work"
	printf 'HOSTSCFG="%s/home/etc/hosts.cfg"\n' "$work"
	printf 'XYMONSERVERHOSTNAME="hosta"\nXYMONSERVERIP="127.0.0.1"\nXYMSRV="127.0.0.1"\n'
	printf 'XYMONDPORT="%s"\n' "$port"
} >"$work/xymonserver.cfg"

pids=""
stop_daemons() {
	local p kids=""
	"$XYMON" "127.0.0.1:$port" shutdown >/dev/null 2>&1 || true
	# and what they forked: the worker serves each connection in a child
	for p in $pids; do kids="$kids $(ps -A -o pid= -o ppid= 2>/dev/null | awk -v p="$p" '$2 == p { print $1 }')"; done
	# shellcheck disable=SC2086  # one pid per word
	stop_process $pids $kids
	for pidfile in "$work/channel.pid" "$work/xymond.pid"; do
		[ -f "$pidfile" ] && stop_process "$(cat "$pidfile")" || true
	done
}
register_cleanup stop_daemons

# The locator on [::1]. It says why it could not start on stderr, before its
# log is open.
"$XYMOND_LOCATOR" --listen="[::1]:$lport" --logfile="$work/logs/locator.log" 2>"$work/logs/locator.err" &
pids="$pids $!"
up=
for _ in {1..50}; do
	grep -q "Locator is available" <<<"$("$LOCATOR" "[::1]:$lport" </dev/null 2>/dev/null)" && { up=1; break; }
	grep -q 'Cannot bind' "$work/logs/locator.err" 2>/dev/null && break
	sleep 0.1
done
[ -n "$up" ] || fail "the locator did not come up on [::1]:$lport: $(cat "$work/logs/locator.err" "$work/logs/locator.log" 2>/dev/null)"

"$XYMONCMD" --env="$work/xymonserver.cfg" "$XYMOND_RRD" --rrddir="$work/rrd1" \
	--listen="[::1]:$p1" --locator="[::1]:$lport" --locatorweight=2 \
	>>"$work/logs/rrd1.log" 2>&1 &
pids="$pids $!"
for _ in {1..100}; do
	grep -q 'Setting up network listener' "$work/logs/rrd1.log" 2>/dev/null && break
	sleep 0.1
done
grep -q 'Setting up network listener' "$work/logs/rrd1.log" 2>/dev/null \
	|| fail "the rrd worker never registered with the locator: $(cat "$work/logs/rrd1.log" 2>/dev/null)"

"$XYMOND" --env="$work/xymonserver.cfg" --hosts="$work/home/etc/hosts.cfg" \
	--listen="127.0.0.1:$port" --pidfile="$work/xymond.pid" --daemon \
	--log="$work/logs/xymond.log"
for _ in {1..100}; do
	"$XYMON" "127.0.0.1:$port" ping >/dev/null 2>&1 && break
	sleep 0.1
done

"$XYMOND_CHANNEL" --env="$work/xymonserver.cfg" --channel=status --debug \
	--locator="[::1]:$lport" --service=rrd \
	--pidfile="$work/channel.pid" --daemon --log="$work/logs/channel.log"
for _ in {1..50}; do [ -s "$work/channel.pid" ] && break; sleep 0.1; done

"$XYMON" "127.0.0.1:$port" "status hosta.cpu green $(date) up: 3 days, 2 users, 120 procs, load=0.50" \
	|| fail "xymond rejected the status"
for _ in {1..150}; do grep -q hosta <<<"$(ls "$work/rrd1")" && break; sleep 0.1; done
grep -q hosta <<<"$(ls "$work/rrd1")" \
	|| fail "the status did not reach the worker on [::1]: channel: $(cat "$work/logs/channel.log") -- worker: $(cat "$work/logs/rrd1.log")"

# Both name the IPv6 address in brackets, so the port reads apart from it
assert_contains "Setting up network listener on [::1]:$p1" "$(cat "$work/logs/rrd1.log")" \
	"the worker's log does not name its listener as [::1]:$p1"
assert_contains "Connecting to peer [::1]:$p1" "$(cat "$work/logs/channel.log")" \
	"the channel's log does not name its peer as [::1]:$p1"

# A local worker is named by its command, as it has no address
"$XYMOND_CHANNEL" --env="$work/xymonserver.cfg" --channel=status --debug \
	--pidfile="$work/local.pid" --daemon --log="$work/logs/local.log" cat
for _ in {1..50}; do [ -s "$work/local.pid" ] && break; sleep 0.1; done
[ -s "$work/local.pid" ] && pids="$pids $(cat "$work/local.pid")"
"$XYMON" "127.0.0.1:$port" "status hosta.disk green $(date) local" || fail "xymond rejected the status"
for _ in {1..50}; do grep -q 'Connecting to peer' "$work/logs/local.log" 2>/dev/null && break; sleep 0.1; done
assert_contains "Connecting to peer cat" "$(cat "$work/logs/local.log" 2>/dev/null)" \
	"the channel's log does not name its local worker by its command"

pass "xymond_channel finds a network worker on [::1] through the locator, delivers to it there, and names its peers in its log"
