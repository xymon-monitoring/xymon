#!/usr/bin/env bash
#
# A dialogue over TLS, both ways round.
#
# [dlgtls] is TLS from the first byte, the "options ssl" case. It is here
# because it broke completely and silently during development: socket_read
# returns a NEGATIVE value with sslagain set when SSL_read wants more data,
# the driver read that as the peer hanging up, and every TLS dialogue
# failed the instant it waited for a greeting. Plain TCP was unaffected
# throughout, and the source-level tests stayed green, so nothing noticed.
#
# [msatls] is explicit TLS: plaintext until the upgrade, encrypted after
# it, same socket. That is submission (587), IMAP (143) and POP3 (110) --
# none of which could be tested at all before, since "options ssl" means
# TLS from the first byte and there was no way to say "start it here".
#
# The peer records the handshake and each line it receives, so the test can
# check that the second EHLO arrived AFTER the upgrade rather than merely
# that the probe reported success.

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

"$CC" -o "$work/peer" "$root/tests/lib/dialogue-peer.c" -lssl -lcrypto 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; skip "dialogue-peer does not compile against libssl"; }

# TLS from the first byte.
cat > "$work/imp.script" <<'EOS'
starttls
send "220 test.local ESMTP\r\n"
recv ehlo
send "250-test.local\r\n250 CHUNKING\r\n"
recv quit
send "221 bye\r\n"
EOS

# Plaintext, then an upgrade part-way.
cat > "$work/exp.script" <<'EOS'
send "220 test.local ESMTP\r\n"
recv ehlo
send "250-test.local\r\n250 STARTTLS\r\n"
recv starttls
send "220 2.0.0 Ready to start TLS\r\n"
starttls
recv ehlo
send "250 OK\r\n"
recv quit
send "221 bye\r\n"
EOS

: > "$work/pids"
start() {
	"$work/peer" "$1" "$3" "$work/c.pem" "$work/k.pem" > "$2" &
	echo $! >> "$work/pids"
	local i=0
	while [ "$i" -lt 50 ]; do [ -s "$2" ] && break; sleep 0.1; i=$((i + 1)); done
	cat "$2"
}
pi=$(start "$work/imp.script" "$work/pi" "$work/oi")
pe=$(start "$work/exp.script" "$work/pe" "$work/oe")
register_cleanup "kill $(tr '\n' ' ' < "$work/pids") 2>/dev/null || :"
[ -n "$pi" ] && [ -n "$pe" ] || fail "a peer never named its port"

cat > "$work/home/etc/protocols.cfg" <<CFG
[dlgtls]
   expect "220"
   send "ehlo xymonnet\r\n"
   expect "250" until "250 "
   send "quit\r\n"
   options ssl,banner
   port $pi

[msatls]
   expect "220"
   send "ehlo xymonnet\r\n"
   expect "250" until "250 "
   send "starttls\r\n"
   expect "220"
   starttls
   send "ehlo xymonnet\r\n"
   expect "250"
   send "quit\r\n"
   options banner
   port $pe
CFG
printf '127.0.0.1\timplicit\t# dlgtls\n127.0.0.1\texplicit\t# msatls\n' > "$work/home/etc/hosts.cfg"

XYMONHOME="$work/home" "$XYMONNET" --no-update --noping --checkresponse \
	--dns=ip --timeout=20 >"$work/out.txt" 2>&1 || :
sleep 1

# --- TLS from the first byte -------------------------------------------------
grep -q '^tls-ok' "$work/oi" || fail "the implicit-TLS peer never completed a handshake"
grep -q '^got ehlo' "$work/oi" || fail \
	"over TLS the dialogue never sent its EHLO. A read that returns 'not yet'
is being taken for end-of-file, so the dialogue gives up while waiting for
the greeting -- plain TCP would not show this:
$(cat "$work/oi")"
grep -q 'Service dlgtls on implicit is OK' "$work/out.txt" || fail \
	"the TLS dialogue did not report the service up:
$(cat "$work/out.txt")"

# --- explicit TLS ------------------------------------------------------------
grep -q '^tls-ok' "$work/oe" || fail \
	"the upgrade never happened: $(cat "$work/oe")"
[ "$(grep -c '^got ehlo' "$work/oe")" -eq 2 ] || fail \
	"expected an EHLO before the upgrade and another after it:
$(cat "$work/oe")"
# The second EHLO must land after the handshake, not before it.
awk '/^tls-ok/{seen=1} /^got ehlo/{if (seen) found=1} END{exit !found}' "$work/oe" || fail \
	"no EHLO arrived after the handshake, so nothing was actually sent over
the upgraded connection:
$(cat "$work/oe")"
grep -q '^got quit' "$work/oe" || fail "the upgraded session never reached QUIT"
grep -q 'Service msatls on explicit is OK' "$work/out.txt" || fail \
	"the starttls dialogue did not report the service up:
$(cat "$work/out.txt")"

pass "a dialogue runs over TLS from the first byte, and over a connection upgraded part-way"
