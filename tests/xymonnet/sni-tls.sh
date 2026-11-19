#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/xymonnet/sni-tls.sh
#
# A TLS service sends the tested name as the TLS servername (SNI), the way an
# https test does, so its "sslcert" check describes the host's own certificate
# and not whatever the endpoint serves by default. The gate is whether the
# service does TLS (svc_uses_tls), not its name, so imaps, pop3s, smtps and the
# rest all send it; a "scheme://name" test overrides the name, an address sends
# none (RFC 6066), and a non-TLS scheme is refused. The servername is read off
# the ClientHello by a tiny TLS peer (tls-sni-peer.c, shared with
# sni-https-behaviour.sh); the peer needs the handshake as the first bytes, so
# the cases use services whose "start tls" is the first step. Skips when
# xymonnet is not built or no C compiler is present.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin XYMONNET xymonnet/xymonnet
require_cc

root=$(find_root)
work=$(mktempdir)
register_cleanup "rm -rf '$work'"

peer="$work/peer"
"$CC" -o "$peer" "$root/tests/xymonnet/tls-sni-peer.c" 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; skip "tls-sni-peer does not compile"; }

mkdir -p "$work/home/etc" "$work/tmp"
cp "$root/xymonnet/protocols.cfg" "$work/home/etc/protocols.cfg"
export XYMONHOME="$work/home" XYMONTMP="$work/tmp"
export XYMONNETSVCS="imaps smtps pop3s imap smtp pop3 http ssh"

# run TAG HOSTLINE EXPECT -- start the peer, point one test at it through
# HOSTLINE (with @PORT@ replaced by the peer's port), and check the SNI it read
# off the ClientHello.
run() {
	local tag=$1 line=$2 expect=$3
	local out="$work/out" port="" got pp i
	rm -f "$out"
	"$peer" 10 > "$out" 2>/dev/null &
	pp=$!
	register_cleanup "stop_process $pp"
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

# A TLS service on the host's own name sends that name as the servername.
run "imaps sends the tested hostname" \
	'127.0.0.1 mail.example.com # imaps:@PORT@' 'mail.example.com'

# The gate is "does TLS", not the service name: pop3s sends it too.
run "pop3s sends the tested hostname" \
	'127.0.0.1 mail.example.com # pop3s:@PORT@' 'mail.example.com'

# scheme://NAME overrides it, for any TLS service: the host is labelled
# "label.example.com" but the servername, and the name the certificate is
# checked against, is the URL's.
run "imaps://name sets the servername" \
	'127.0.0.1 label.example.com # imaps://mail.example.com:@PORT@' 'mail.example.com'
run "smtps://name sets the servername" \
	'127.0.0.1 label.example.com # smtps://real.example.com:@PORT@' 'real.example.com'

# A servername written as an address sends none, as RFC 6066 requires.
run "scheme://address sends no servername" \
	'127.0.0.1 label.example.com # imaps://127.0.0.1:@PORT@' ''

# A non-TLS scheme names no TLS service, so the test is refused, not run: smtp
# is plaintext. xymonnet logs the refusal and creates no test.
printf '127.0.0.1 host.example.com # smtp://real.example.com:9\n' > "$work/home/etc/hosts.cfg"
reject=$(XYMONHOME="$work/home" "$XYMONNET" --noping --no-update --timeout=2 2>&1 || :)
grep -qiF "'smtp://' is not a TLS service" <<<"$reject" \
	|| fail "a non-TLS scheme in a URL was not refused; xymonnet said:
$reject"

pass "every TLS service sends the tested name, a scheme://name overrides it, an address sends none, and a non-TLS scheme is refused"
