#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/xymonproxy-addresses.sh
#
# xymonproxy's --listen and --server take an address of either family (#116):
# IPv4 as before, an IPv4 short form such as 127.1 as inet_aton() took it, a
# name, and [IPv6]:PORT. A malformed one, or one that does not resolve, is
# refused with a message naming it as given, and the proxy goes on with the
# good ones.
#
# Both options were read as IPv4 only, split at the first ':' and given to
# inet_aton(), so an IPv6 address was refused.
#
# A xymond on 127.0.0.1, and on [::1] where the host has it; a xymonproxy in
# front of it; a status sent through the proxy, and the board asked whether it
# arrived, and whether it says it came from the client's address. A server by
# name is checked where /etc/hosts makes localhost 127.0.0.1; without ::1 the
# IPv6 part is reported as not checked.

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

PROXY_PID=""
stop_proxy() {
	[ -n "$PROXY_PID" ] || return 0
	stop_process "$PROXY_PID"
	PROXY_PID=""
}
register_cleanup stop_proxy

# proxy LISTEN ARGS... -- a xymonproxy on LISTEN and a free port, PPORT; tries
# another port when the one picked could not be bound
proxy() {
	local listen=$1 try
	shift
	for try in 1 2 3 4 5; do
		PPORT=$(free_port)
		"$XYMONPROXY" --no-daemon --listen="$listen:$PPORT" "$@" >"$work/proxy.log" 2>&1 &
		PROXY_PID=$!
		for _ in {1..50}; do
			grep -q 'Sending to Xymon' "$work/proxy.log" 2>/dev/null && return 0
			kill -0 "$PROXY_PID" 2>/dev/null || break
			sleep 0.1
		done
		stop_proxy
		grep -q 'Cannot bind' "$work/proxy.log" 2>/dev/null || break
	done
	fail "xymonproxy did not start on $listen: $(cat "$work/proxy.log")"
}

# relay LISTEN SERVER TEST FROM -- a proxy with --listen=LISTEN:PPORT
# --server=SERVER, a status for TEST sent to it at LISTEN, and the board asked
# for it; xymond must have it from FROM, the client's address
relay() {
	local listen=$1 server=$2 test=$3 from=$4 got=
	proxy "$listen" --server="$server"
	XYMON_TIMEOUT=5 "$XYMONCLIENT" "$listen:$PPORT" "status relayhost.$test green relayed" >/dev/null 2>&1 \
		|| fail "the proxy on $listen:$PPORT refused the status: $(cat "$work/proxy.log")"
	for _ in {1..50}; do
		got=$(XYMON_TIMEOUT=5 "$XYMONCLIENT" "127.0.0.1:$XPORT" "xymondboard host=relayhost test=$test fields=color" 2>/dev/null || true)
		[ "$got" = green ] && break
		sleep 0.1
	done
	PROXYLOG=$(cat "$work/proxy.log")
	stop_proxy
	[ "$got" = green ] || fail "the status sent through $listen to $server did not reach xymond: board '$got', proxy: $PROXYLOG"
	# xymond takes the sender from the proxy's "Status message received from"
	got=$(XYMON_TIMEOUT=5 "$XYMONCLIENT" "127.0.0.1:$XPORT" "xymondboard host=relayhost test=$test fields=sender" 2>/dev/null || true)
	[ "$got" = "$from" ] || fail "the status relayed says it came from '$got', not $from"
}

# IPv4, and the short form inet_aton() took
relay 127.1 "127.1:$XPORT" short 127.0.0.1
assert_contains "Listening on 127.0.0.1:$PPORT" "$PROXYLOG" "xymonproxy did not listen on 127.1"
partial=
# A server by name, where /etc/hosts makes localhost 127.0.0.1
if grep -qE '^[[:space:]]*127\.0\.0\.1[[:space:]](.*[[:space:]])?localhost([[:space:]]|$)' /etc/hosts 2>/dev/null; then
	relay 127.0.0.1 "localhost:$XPORT" byname 127.0.0.1
	assert_contains "Sending to Xymon server(s) 127.0.0.1:$XPORT" "$PROXYLOG" "xymonproxy did not resolve the server localhost"
else
	partial="a server by name: /etc/hosts does not map localhost to 127.0.0.1"
fi

# Malformed: refused, with the address as given
out=$("$XYMONPROXY" --no-daemon --listen="[::1]junk" --server="127.0.0.1:$XPORT" 2>&1 || true)
assert_contains "Invalid listen address [::1]junk" "$out" "a malformed --listen was not refused with its value"
# A malformed --server, and one that does not resolve, are left out; the
# proxy goes on with the good one, and names each refused one as given
proxy 127.0.0.1 --server="127.0.0.1:junk,no-such-host.invalid:1984,127.0.0.1:1"
kill -0 "$PROXY_PID" 2>/dev/null || fail "xymonproxy stopped over a bad --server: $(cat "$work/proxy.log")"
stop_proxy
assert_contains "Invalid remote address 127.0.0.1:junk" "$(cat "$work/proxy.log")" "a malformed --server was not refused"
assert_contains "Invalid remote address no-such-host.invalid:1984" "$(cat "$work/proxy.log")" \
	"a --server that does not resolve was not refused with its value"
assert_contains "Sending to Xymon server(s) 127.0.0.1:1 " "$(cat "$work/proxy.log")" "xymonproxy did not keep the good --server"

claim="xymonproxy takes IPv4, its short forms and names, refuses a malformed address, and says where a status came from"
if [ "$HAVE_LO6" = yes ]; then
	relay "[::1]" "[::1]:$XPORT" v6 ::1
	assert_contains "Listening on [::1]:$PPORT" "$PROXYLOG" "xymonproxy did not listen on [::1]"
	assert_contains "Sending to Xymon server(s) [::1]:$XPORT" "$PROXYLOG" "xymonproxy did not take the IPv6 server"
	claim="$claim, and relays from [::1] to a xymond on [::1]"
else
	partial="${partial:+$partial; }the IPv6 relay: this host has no IPv6 loopback (::1)"
fi
if [ -n "$partial" ]; then pass_partial "$claim" "$partial"; else pass "$claim"; fi
