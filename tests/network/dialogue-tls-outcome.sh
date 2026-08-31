#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# What a failed TLS upgrade means is the entry's decision, not the driver's.
#
# A server that REFUSES to upgrade answers 454 and the entry routes that
# where it likes -- dialogue-starttls-negotiation.sh covers it. A server that
# AGREES and then fails the handshake had no such route: the test ended red,
# whatever the entry said. Those are the same operational fact -- TLS is not
# usable, the service is up -- and only one of them could be reported as such.
#
# "start tls" binds its outcome as ${tls}: "ok", or the reason OpenSSL gave
# up. The state that starts it then decides, with the same "~" edges every
# other bound value uses.
#
# THE CONTROL is [tlsgood]: the same entry against a peer that completes the
# handshake. It must be green -- if both peers came out yellow, the routing
# would be reporting the outcome it was handed rather than the one that
# happened.

set -eu
. "$(dirname "$0")/../lib/assert.sh"
root=$(find_root)

require_bin XYMONNET xymonnet/xymonnet
require_cc

work=$(mktempdir); register_cleanup "rm -rf '$work'"
mkdir -p "$work/home/etc"

"$CC" -o "$work/peer" "$root/tests/lib/dialogue-peer.c" -lssl -lcrypto 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; skip "dialogue-peer does not compile against libssl"; }

# Agrees to STARTTLS, then speaks plaintext where the ClientHello's answer
# belongs: the handshake fails on a server that is otherwise answering.
printf '%s\n' 'send "220 broken.local ESMTP\r\n"' \
	      'recv ehlo' \
	      'send "250-broken.local\r\n250 STARTTLS\r\n"' \
	      'recv starttls' \
	      'send "220 go ahead\r\n"' \
	      'send "not a ServerHello at all\r\n"' \
	      'hold 20'                                  > "$work/bad.script"
# The same conversation, upgrading properly.
printf '%s\n' 'send "220 good.local ESMTP\r\n"' \
	      'recv ehlo' \
	      'send "250-good.local\r\n250 STARTTLS\r\n"' \
	      'recv starttls' \
	      'send "220 go ahead\r\n"' \
	      'starttls' \
	      'recv quit' \
	      'send "221 bye\r\n"' \
	      'hangup'                                   > "$work/good.script"

: > "$work/pids"
start_peer() {	# script portfile obsfile
	"$work/peer" "$1" "$3" "$work/cert.pem" "$work/key.pem" > "$2" &
	echo $! >> "$work/pids"
	local i=0
	while [ "$i" -lt 50 ]; do [ -s "$2" ] && break; sleep 0.1; i=$((i + 1)); done
	cat "$2"
}
command -v openssl >/dev/null 2>&1 || skip "openssl CLI needed for the peer certificate"
openssl req -x509 -newkey rsa:2048 -keyout "$work/key.pem" -out "$work/cert.pem" \
	-days 400 -nodes -subj "/CN=good.local" >/dev/null 2>&1 || skip "cannot make a certificate"

pbad=$(start_peer  "$work/bad.script"  "$work/pb" "$work/ob")
pgood=$(start_peer "$work/good.script" "$work/pg" "$work/og")
register_cleanup "kill $(tr '\n' ' ' < "$work/pids") 2>/dev/null || :"
[ -n "$pbad" ] && [ -n "$pgood" ] || fail "a peer never named its port"

entry() {	# name port
cat <<CFG
[$1]
   options banner
   port $2
   begin greeting

   state greeting
      timeout(10)                  -> fail
      expect "220"                 -> ehlo

   state ehlo
      send "ehlo xymonnet\r\n"
      timeout(10)                  -> fail
      expect "250" until "250 "    -> upgrade

   state upgrade
      send "starttls\r\n"
      timeout(10)                  -> fail
      expect "220"                 -> secure

   state secure
      start tls
      tls_code ~ "^ok"             -> farewell
      else                         -> warning

   state farewell
      send "quit\r\n"
      timeout(10)                  -> fail
      eof                          -> success
      expect "221"                 -> success
CFG
}
{ entry tlsbroken "$pbad"; entry tlsgood "$pgood"; } > "$work/home/etc/protocols.cfg"
printf '127.0.0.1\tbad\t# tlsbroken\n127.0.0.1\tgood\t# tlsgood\n' > "$work/home/etc/hosts.cfg"

XYMONHOME="$work/home" "$XYMONNET" --no-update --noping --checkresponse=red \
	--dns=ip --timeout=30 >"$work/out.txt" 2>&1 || :

colour_of() { grep -oE "status\+[0-9]+ $1 (green|yellow|red|clear)" "$work/out.txt" | awk '{print $3}' | head -1; }

[ "$(colour_of bad.tlsbroken)" = yellow ] || fail \
	"a server that agreed to STARTTLS and then failed the handshake reported
'$(colour_of bad.tlsbroken)', not yellow. The entry routes that outcome with
'else -> warning', so the handshake result is not reaching the edges:
$(grep -i tlsbroken "$work/out.txt" | head -4)"

# THE CONTROL
[ "$(colour_of good.tlsgood)" = green ] || fail \
	"the peer that completed the handshake did not go green, so the routing is
answering the same way whatever happened:
$(grep -i tlsgood "$work/out.txt" | head -4)"

pass "a failed TLS upgrade is routed by the entry, and a successful one still passes"
