#!/usr/bin/env bash
#
# The companion to ssl-write-retry.sh, which reads the source. This one
# makes SSL_write() actually defer and checks whether the bytes arrive.
#
# Forcing the condition needs two things at once: a payload larger than the
# socket buffers, and a peer that does not read for a moment. Then
# SSL_write() takes part of it and returns WANT_WRITE for the rest. What
# happens next is the point -- the remainder is either retried or silently
# dropped -- and it is invisible in the source: both versions "send" the
# data and neither reports an error. Only the peer knows what it got.
#
# The probe's real sends are a few dozen bytes and would never trigger
# this, which is why the bug survived: it needs a slow reader on the far
# side, which is what a loaded mail server looks like.

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

# Size matters and is not arbitrary. Swept against a build WITHOUT the fix:
# 1MB was absorbed by the socket buffers and never deferred at all -- the
# test passed on the bug, a false green. 4MB and 16MB both truncated. 8MB is
# the smallest round size with margin either side. If this ever stops
# failing on an unfixed build, the payload has become too small for the
# host's buffers rather than the bug having gone away.
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

# One send far larger than any socket buffer, so SSL_write cannot take it in
# a single call while the peer is not reading.
{
	printf '[bigsend]\n   send "'
	awk -v n="$payload" 'BEGIN { s = sprintf("%*s", n, ""); gsub(/ /, "A", s); printf "%s", s }'
	printf '"\n   expect "OK"\n   options ssl,banner\n   port %s\n' "$port"
} > "$work/home/etc/protocols.cfg"
printf '127.0.0.1\tpeer\t# bigsend\n' > "$work/home/etc/hosts.cfg"

XYMONHOME="$work/home" "$XYMONNET" --no-update --noping --dns=ip \
	--timeout=25 >"$work/out.txt" 2>&1 || :
wait "$peer" 2>/dev/null || :

[ -s "$work/got.txt" ] || fail "the peer recorded nothing -- it never completed a TLS handshake"
got=$(cat "$work/got.txt")
case $got in
	ERR*) skip "the peer could not run the TLS exchange: $got" ;;
esac

[ "$got" -eq "$payload" ] || fail \
	"the peer received $got of $payload bytes. SSL_write() could not take the
whole buffer while the peer was not reading, and the remainder was dropped
instead of retried. The probe reports no error, so this is silent data loss
against any slow reader (#451)."

pass "a deferred SSL_write is retried, not dropped: all $payload bytes arrived (#451)"
