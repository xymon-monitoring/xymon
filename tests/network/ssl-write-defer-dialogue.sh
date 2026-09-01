#!/usr/bin/env bash
# The same deferred SSL_write, on the dialogue path.
#
# ssl-write-defer-behaviour.sh proves the LEGACY write retries what
# SSL_write() could not take. Its entry -- one send, one expect -- is not
# classified as a dialogue, so the step-list path had no coverage: it
# advanced the step on any non-error, and WANT_WRITE returns 0.
#
# Two sends is the cheapest way to be a dialogue, and needs no greeting.

set -eu
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

"$CC" -o "$work/peer" "$root/tests/lib/tls-slow-reader.c" -lssl -lcrypto 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; skip "tls-slow-reader does not compile against libssl"; }

# The size is chosen in ssl-write-defer-behaviour.sh, and for the same reason:
# smaller payloads are absorbed by the socket buffers, never defer, and pass
# on the bug.
payload=${XYMON_TEST_SSLWRITE_BYTES:-8000000}

"$work/peer" "$work/c.pem" "$work/k.pem" "$work/got.txt" > "$work/port" &
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

# Two sends, so the entry is driven as a dialogue. The first is one byte; the
# second is the one that cannot be written in a single call.
{
	printf '[bigdialogue]\n   send "A"\n   send "'
	# dd and tr, not awk: BSD awk's gsub() is quadratic in the subject length.
	dd if=/dev/zero bs="$payload" count=1 2>/dev/null | tr '\0' 'B'
	printf '"\n   expect "OK"\n   options ssl,banner\n   port %s\n' "$port"
} > "$work/home/etc/protocols.cfg"
printf '127.0.0.1\tpeer\t# bigdialogue\n' > "$work/home/etc/hosts.cfg"

XYMONHOME="$work/home" "$XYMONNET" --no-update --noping --dns=ip \
	--timeout=25 >"$work/out.txt" 2>&1 || :
wait "$peer" 2>/dev/null || :

[ -s "$work/got.txt" ] || fail "the peer recorded nothing -- it never completed a TLS handshake"
got=$(cat "$work/got.txt")
case $got in
	ERR*) skip "the peer could not run the TLS exchange: $got" ;;
esac

want=$((payload + 1))
[ "$got" -eq "$want" ] || fail \
	"the peer received $got of $want bytes. A dialogue step advanced before its
buffer had all gone out, so the command was truncated -- or, on WANT_WRITE,
never sent at all -- and the probe then waited for a reply to something the
peer never got. Nothing reports an error, so it is silent against any slow
reader."

pass "a dialogue send finishes before the step advances: all $want bytes arrived"
