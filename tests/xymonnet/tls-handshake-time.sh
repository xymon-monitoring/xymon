#!/usr/bin/env bash
#
# A TLS service's response time includes its handshake. The probe stops the
# clock once at the TCP connect and again when SSL_connect() completes, and
# the second reading is what "Seconds:" reports and the tcp response-time
# graph records.
#
# Which arm of the select() loop completes the handshake depends on the
# direction OpenSSL waits in, and since pending handshakes are registered
# for readability (#452) it is usually the read arm. A completion there that
# does not take the second reading reports the TCP connect alone: every
# smtps, imaps, pop3s and "options ssl" graph drops by the handshake time,
# with nothing in the status saying why.
#
# The peer accepts the TCP connection at once and holds the handshake back,
# so the two readings are far apart and the assertion needs no tight bound.

set -euo pipefail
. "$(dirname "$0")/../lib/assert.sh"
root=$(find_root)

require_bin XYMONNET xymonnet/xymonnet
require_cc
command -v openssl >/dev/null 2>&1 || skip "openssl is needed to make a test certificate"

work=$(mktempdir); register_cleanup "rm -rf '$work'"
mkdir -p "$work/home/etc"

openssl req -x509 -newkey rsa:2048 -keyout "$work/k.pem" -out "$work/c.pem" \
	-days 2 -nodes -subj "/CN=127.0.0.1" >"$work/ssl.log" 2>&1 \
	|| skip "openssl could not generate a test certificate"

"$CC" -o "$work/peer" "$root/tests/xymonnet/tls-handshake-time-harness.c" -lssl -lcrypto 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; skip "tls-handshake-time-harness does not compile against libssl"; }

delay_ms=500
floor=0.4

"$work/peer" "$work/c.pem" "$work/k.pem" "$delay_ms" > "$work/port" &
peer=$!
register_cleanup "kill $peer 2>/dev/null || :"

i=0
while [ "$i" -lt 50 ]; do
	[ -s "$work/port" ] && break
	kill -0 "$peer" 2>/dev/null || fail "the peer exited before naming its port"
	sleep 0.1
	i=$((i + 1))
done
port=$(cat "$work/port")
[ -n "$port" ] || fail "the peer never named a port"

printf '[slowtls]\n   options ssl,banner\n   port %s\n' "$port" > "$work/home/etc/protocols.cfg"
printf '127.0.0.1\tpeer\t# slowtls\n' > "$work/home/etc/hosts.cfg"

XYMONHOME="$work/home" "$XYMONNET" --no-update --noping --dns=ip \
	--timeout=10 >"$work/out.txt" 2>&1 || :
wait "$peer" 2>/dev/null || :

grep -q 'Service slowtls on peer is OK' "$work/out.txt" \
	|| fail "the TLS test did not come up green, so its time says nothing:
$(cat "$work/out.txt")"

secs=$(awk '/^Seconds:/ { print $2; exit }' "$work/out.txt")
[ -n "$secs" ] || fail "the slowtls status carries no Seconds: line"

awk -v s="$secs" -v f="$floor" 'BEGIN { exit !(s + 0 >= f + 0) }' || fail \
	"the handshake took ${delay_ms}ms but Seconds: reports $secs: the handshake
time is missing. The clock was stopped at the TCP connect and not again when
SSL_connect() completed, so the response time and its graph show the connect
alone."

pass "a TLS service's Seconds: includes its handshake ($secs s for a ${delay_ms}ms handshake)"
