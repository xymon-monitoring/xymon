#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/network/sni-tcp-tls-behaviour.sh
#
# Behavioural companion to sni-tcp-tls.sh. That one is a static guard that the
# wiring is present; this one runs the built xymonnet against a peer that
# reports the SNI server name from the ClientHello, and checks the name
# actually put on the wire for a plain TCP-TLS (imaps) test.
#
# Sibling of #453/#454's tls-handshake-wait.sh / ssl-write-retry.sh: a compiled
# 127.0.0.1 peer driven by the real probe. --dns=ip pins the connection to the
# loopback address in column 1, so the hosts.cfg name need not resolve. Skips
# when xymonnet is not built or no C compiler is present.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin XYMONNET xymonnet/xymonnet
require_cc

root=$(find_root)
work=$(mktempdir)
register_cleanup "rm -rf '$work'"

peer="$work/peer"
"$CC" -o "$peer" "$root/tests/lib/tls-sni-peer.c" 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; skip "tls-sni-peer does not compile"; }

# Minimal XYMONHOME: imaps must be a known SSL service.
mkdir -p "$work/home/etc" "$work/tmp"
cp "$root/xymonnet/protocols.cfg" "$work/home/etc/protocols.cfg"
export XYMONHOME="$work/home" XYMONTMP="$work/tmp"
export XYMONNETSVCS="imaps smtps pop3s imap smtp pop3 http ssh"

# run TAG HOSTLINE EXPECT -- start the peer, point one imaps test at it via
# HOSTLINE (with @PORT@ replaced by the peer's port), and check the SNI it saw.
run() {
	local tag=$1 line=$2 expect=$3
	local out="$work/out" port="" got pp
	rm -f "$out"
	"$peer" 10 > "$out" 2>/dev/null &
	pp=$!
	register_cleanup "kill $pp 2>/dev/null || :"
	local i
	for i in $(seq 1 50); do
		port=$(sed -n '1p' "$out" 2>/dev/null || true)
		[ -n "$port" ] && break
		kill -0 "$pp" 2>/dev/null || fail "$tag: peer exited before naming its port"
		sleep 0.1
	done
	[ -n "$port" ] || fail "$tag: peer never named a port"
	printf '%s\n' "${line//@PORT@/$port}" > "$work/home/etc/hosts.cfg"
	"$XYMONNET" --noping --no-update --dns=ip --timeout=5 >/dev/null 2>&1 || :
	wait "$pp" 2>/dev/null || :
	got=$(sed -n '2p' "$out" 2>/dev/null | sed 's/^SNI=//')
	[ "$got" = "$expect" ] || fail "$tag: expected SNI '$expect', the peer saw '$got'"
}

run "default sends the hostname"    '127.0.0.1 host.example.com # imaps:@PORT@'        'host.example.com'
run "nosni suppresses it"           '127.0.0.1 host.example.com # imaps:@PORT@ nosni'  ''
run "a testip host sends no SNI"     '127.0.0.1 host.example.com # testip imaps:@PORT@' ''
run "an IP-literal name sends none"  '127.0.0.1 127.0.0.1 # imaps:@PORT@'               ''

pass "xymonnet puts the right SNI on the wire: hostname by default; none for nosni, a testip host, or an IP-literal name"
