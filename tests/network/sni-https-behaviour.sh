#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/network/sni-https-behaviour.sh
#
# The servername an https test puts on the wire, read off the ClientHello by a
# compiled 127.0.0.1 peer driven by the real xymonnet.
#
# Three things are pinned, and the first two are the reason the third exists:
# the name comes from the URL and not from the host's entry, an address in the
# URL sends no servername at all (RFC 6066), and the per-host sni/nosni tags do
# not reach this path -- they are per host while the name is per URL, and one
# host may carry several URLs with different names.
#
# Sibling of #453/#454's tls-handshake-wait.sh / ssl-write-retry.sh. The URL's
# name need not resolve: an http test resolves the URL's own host (httptest.c),
# not column 1, so the address is supplied as its own field. A URL's network part
# is "[username:password@]hostname[:port][=forcedIP]" (lib/url.c), and the forced
# IP is read before the port -- so the name stays the name, in the handshake and
# the Host: header, while the socket goes to the peer. Skips when xymonnet is not
# built or no C compiler is present.

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

# Minimal XYMONHOME.
mkdir -p "$work/home/etc" "$work/tmp"
cp "$root/xymonnet/protocols.cfg" "$work/home/etc/protocols.cfg"
export XYMONHOME="$work/home" XYMONTMP="$work/tmp"
export XYMONNETSVCS="imaps smtps pop3s imap smtp pop3 http ssh"

# run TAG HOSTLINE EXPECT -- start the peer, point one https test at it via
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
	"$XYMONNET" --noping --no-update --timeout=5 >/dev/null 2>&1 || :
	wait "$pp" 2>/dev/null || :
	got=$(sed -n '2p' "$out" 2>/dev/null | sed 's/^SNI=//')
	[ "$got" = "$expect" ] || fail "$tag: expected SNI '$expect', the peer saw '$got'"
}

# The URL's name, not the host's: the entry is called host.example.com and the
# name on the wire must be www.example.com.
run "the name comes from the URL" \
	'127.0.0.1 host.example.com # https://www.example.com:@PORT@=127.0.0.1/' 'www.example.com'

# An address in the URL is how a test asks for no servername.
run "an address in the URL sends none" \
	'127.0.0.1 host.example.com # https://127.0.0.1:@PORT@/'       ''

# The host-level tags do not reach an https test: the name is per URL.
run "nosni does not suppress it" \
	'127.0.0.1 host.example.com # https://www.example.com:@PORT@=127.0.0.1/ nosni' 'www.example.com'

pass "an https test sends the URL's host as SNI, sends none for an address, and ignores the per-host sni/nosni tags"
