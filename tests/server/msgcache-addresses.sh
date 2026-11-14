#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/msgcache-addresses.sh
#
# Pulling client data takes an address of either family (#116): msgcache's
# --listen and --server, and xymonfetch's "pulldata=" and --server, read IPv4
# as before, an IPv4 short form such as 127.1 as inet_aton() took it, and
# [IPv6]:PORT. A malformed one is refused with a message, not a crash.
#
# All of them read IPv4 only: msgcache refused an IPv6 --listen and checked the
# puller as an in_addr, and xymonfetch split pulldata at the first ':' and
# connected with inet_aton().
#
# A status is sent into msgcache; xymonfetch pulls it and hands it to xymond,
# whose board then shows it, with the address it was pulled from as its
# sender. Once with IPv4 short forms; where the host has ::1, once over [::1];
# and where IPv6 sockets also reach IPv4-mapped addresses (not under
# IPV6_V6ONLY, the default on FreeBSD), once over [::ffff:127.0.0.1], whose
# sender must read 127.0.0.1. Each IPv6 case left out is reported as not
# checked. And for each family, a pull from an address msgcache's --server
# does not list is refused ("Rejected pullclient request") and the status
# stays off the board.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
# shellcheck source=tests/lib/xymond-daemon.sh
. "$(dirname "$0")/../lib/xymond-daemon.sh"

require_bin XYMOND xymond/xymond
require_bin XYMONFETCH xymond/xymonfetch
require_bin MSGCACHE client/msgcache
require_bin XYMONCLIENT common/xymon
require_shm_segments "$(grep -c 'setup_channel(C_[A-Z_]*, CHAN_MASTER)' "$(find_root)/xymond/xymond.c")"
require_cfg XYMONSERVER_CFG xymond/etcfiles/xymonserver.cfg

work=$(mktempdir)
mkdir -p "$work/home/tmp"
printf '127.0.0.1 cachehost # conn\n' >"$work/hosts.cfg"
sed -e 's|^XYMONHOME=.*|XYMONHOME="'"$work"'/home"|' \
    -e 's|^XYMONTMP=.*|XYMONTMP="'"$work"'/home/tmp"|' \
	"$XYMONSERVER_CFG" >"$work/xymonserver.cfg"

HAVE_LO6=no
have_ipv6_loopback && HAVE_LO6=yes

XYMOND_PID=""
stop_xymond() {
	[ -n "$XYMOND_PID" ] || return 0
	kill "$XYMOND_PID" 2>/dev/null || true
	wait "$XYMOND_PID" 2>/dev/null || true
	XYMOND_PID=""
}
register_cleanup stop_xymond
xymond_launch() {
	local port=$1 listen
	shift
	listen="127.0.0.1:$port"
	[ "$HAVE_LO6" = no ] || listen="$listen,[::1]:$port"
	"$XYMOND" --no-daemon --no-loopback --listen="$listen" \
		--hosts="$work/hosts.cfg" --env="$work/xymonserver.cfg" \
		--pidfile="$work/xymond.pid" --checkpoint-file="$work/chk.out" \
		"$@" >"$work/xymond.log" 2>&1 &
	XYMOND_PID=$!
}
start_xymond
XPORT=$PORT

PIDS=""
register_cleanup 'stop_process $PIDS'

# pull CACHEADDR PULLADDR SERVER ALLOW TEST [SENDER] -- msgcache on CACHEADDR
# letting ALLOW pull, a status for TEST sent into it, and xymonfetch told
# "pulldata=PULLADDR:PORT" and --server=SERVER; the board must then show TEST,
# from SENDER, the address pulled from (default: ALLOW, the same on the
# loopback). ALLOW is the puller's address as it connects:
# msgcache's --server list takes full addresses, as xymond's sender lists do.
# With ALLOW an address the puller does not have, the pull must be refused
# instead: pass "refused" as SENDER.
pull() {
	local cache=$1 pulladdr=$2 server=$3 allow=$4 test=$5 sender=${6:-$4} mport mpid fpid got=
	mport=$(free_port)
	"$MSGCACHE" --no-daemon --listen="$cache:$mport" --server="$allow" --pidfile="$work/msgcache.pid" \
		>"$work/msgcache.log" 2>&1 &
	mpid=$!
	PIDS="$PIDS $mpid"
	for _ in {1..50}; do
		grep -q 'Listening on' "$work/msgcache.log" 2>/dev/null && break
		kill -0 "$mpid" 2>/dev/null || break
		sleep 0.1
	done
	XYMON_TIMEOUT=5 "$XYMONCLIENT" "$cache:$mport" "status cachehost.$test green pulled" >/dev/null 2>&1 \
		|| { stop_process "$mpid"; fail "msgcache on $cache:$mport refused the status: $(cat "$work/msgcache.log")"; }

	printf '127.0.0.1 cachehost # pulldata=%s:%s\n' "$pulladdr" "$mport" >"$work/fetchhosts.cfg"
	XYMONHOME="$work/home" XYMONTMP="$work/home/tmp" HOSTSCFG="!$work/fetchhosts.cfg" XYMONDPORT="$XPORT" \
		"$XYMONFETCH" --server="$server" --interval=1 >"$work/fetch.log" 2>&1 &
	fpid=$!
	PIDS="$PIDS $fpid"
	for _ in {1..100}; do
		got=$(XYMON_TIMEOUT=5 "$XYMONCLIENT" "127.0.0.1:$XPORT" "xymondboard host=cachehost test=$test fields=color" 2>/dev/null || true)
		[ "$got" = green ] && break
		[ "$sender" = refused ] && grep -q 'Rejected pullclient request' "$work/msgcache.log" 2>/dev/null && break
		sleep 0.1
	done
	CACHELOG=$(cat "$work/msgcache.log")
	stop_process "$fpid" "$mpid"
	PIDS=""
	if [ "$sender" = refused ]; then
		[ "$got" != green ] || fail "a puller msgcache's --server=$allow does not list got the status through $pulladdr"
		assert_contains "Rejected pullclient request" "$CACHELOG" "msgcache did not refuse a puller its --server=$allow does not list"
		return 0
	fi
	[ "$got" = green ] \
		|| fail "the status cached on $cache was not pulled through $pulladdr to $server: board '$got' -- fetch: $(cat "$work/fetch.log") -- msgcache: $CACHELOG"
	got=$(XYMON_TIMEOUT=5 "$XYMONCLIENT" "127.0.0.1:$XPORT" "xymondboard host=cachehost test=$test fields=sender" 2>/dev/null || true)
	[ "$got" = "$sender" ] || fail "the status pulled through $pulladdr came from '$got', not $sender"
}

# IPv4 short forms, as inet_aton() took them
pull 127.1 127.1 127.1 127.0.0.1 short
assert_contains "Listening on 127.0.0.1:" "$CACHELOG" "msgcache did not listen on 127.1"
# A puller --server does not list
pull 127.0.0.1 127.0.0.1 127.0.0.1 192.0.2.1 nope4 refused

# Malformed: msgcache refuses it with the value given; xymonfetch reports a
# malformed pulldata and keeps running
out=$("$MSGCACHE" --no-daemon --listen="[::1]junk" 2>&1 || true)
assert_contains "Invalid listen address [::1]junk" "$out" "msgcache did not refuse a malformed --listen"
printf '127.0.0.1 cachehost # pulldata=[::1]junk\n' >"$work/fetchhosts.cfg"
XYMONHOME="$work/home" XYMONTMP="$work/home/tmp" HOSTSCFG="!$work/fetchhosts.cfg" XYMONDPORT="$XPORT" \
	"$XYMONFETCH" --server=127.0.0.1 --interval=1 --debug >"$work/fetch.log" 2>&1 &
fpid=$!
PIDS="$PIDS $fpid"
for _ in {1..50}; do grep -q 'Invalid client IP' "$work/fetch.log" 2>/dev/null && break; kill -0 "$fpid" 2>/dev/null || break; sleep 0.1; done
kill -0 "$fpid" 2>/dev/null || fail "xymonfetch stopped on a malformed pulldata: $(cat "$work/fetch.log")"
stop_process "$fpid"
PIDS=""
assert_contains "Invalid client IP: [::1]junk" "$(cat "$work/fetch.log")" "xymonfetch did not report a malformed pulldata"

claim="client data is pulled with IPv4 short forms, and a malformed address is refused without a crash"
if [ "$HAVE_LO6" = yes ]; then
	pull "[::1]" "[::1]" ::1 ::1 v6
	assert_contains "Listening on [::1]:" "$CACHELOG" "msgcache did not listen on [::1]"
	pull "[::1]" "[::1]" ::1 2001:db8::1 nope6 refused
	claim="$claim, a puller not listed is refused, and over [::1] from msgcache to xymond"
	# Pulled over the IPv4-mapped address: the sender is the IPv4 address.
	# Only where an IPv6 socket reaches it: xymond listens on 127.0.0.1.
	if XYMON_TIMEOUT=5 "$XYMONCLIENT" "[::ffff:127.0.0.1]:$XPORT" ping >/dev/null 2>&1; then
		pull 127.0.0.1 "[::ffff:127.0.0.1]" 127.0.0.1 127.0.0.1 mapped 127.0.0.1
		pass "$claim"
	else
		pass_partial "$claim" "the IPv4-mapped pull: IPv6 sockets do not reach ::ffff:127.0.0.1 here"
	fi
else
	pass_partial "$claim" "the IPv6 pull: this host has no IPv6 loopback (::1)"
fi
