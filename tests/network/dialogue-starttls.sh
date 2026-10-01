#!/usr/bin/env bash
# tests/network/dialogue-starttls.sh
#
# Explicit TLS: plaintext, then an upgrade mid-conversation.
#
# "options ssl" is TLS from the first byte, a different port and service.
# SMTP on 25, submission on 587 and IMAP on 143 open in plaintext and
# upgrade with STARTTLS -- which is what most mail actually uses. Until
# "start tls" existed those services could not be checked past the greeting,
# and could never reach a certificate.
#
# The driver end to end, against a server that speaks the protocol.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

root=$(find_root)
require_bin XYMONNET xymonnet/xymonnet
: "${CC:=cc}"
command -v "$CC" >/dev/null 2>&1 || skip "no C compiler available (CC=$CC)"
command -v openssl >/dev/null 2>&1 || skip "openssl CLI needed for the test certificate"

work=$(mktempdir); register_cleanup "rm -rf '$work'"
mkdir -p "$work/home/etc"

openssl req -x509 -newkey rsa:2048 -keyout "$work/key.pem" -out "$work/cert.pem" \
	-days 30 -nodes -subj "/CN=mail.test.local" >"$work/ssl.log" 2>&1 \
	|| skip "openssl could not generate a test certificate"

"$CC" -o "$work/peer" "$root/tests/lib/dialogue-peer.c" -lssl -lcrypto 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; skip "dialogue-peer does not compile against libssl"; }

# A server that upgrades properly, and one that agrees and then does not.
printf '%s\n' 'send "220 mail.test.local ESMTP\r\n"' \
	      'recv ehlo' \
	      'send "250-mail.test.local\r\n250 STARTTLS\r\n"' \
	      'recv starttls' \
	      'send "220 2.0.0 Ready to start TLS\r\n"' \
	      'starttls' \
	      'recv ehlo' \
	      'send "250 mail.test.local\r\n"' \
	      'hangup'                                   > "$work/good.script"

printf '%s\n' 'send "220 mail.test.local ESMTP\r\n"' \
	      'recv ehlo' \
	      'send "250-mail.test.local\r\n250 STARTTLS\r\n"' \
	      'recv starttls' \
	      'send "220 2.0.0 Ready to start TLS\r\n"' \
	      'send "this is not a ServerHello\r\n"' \
	      'hold 10'                                  > "$work/bad.script"

# A server whose go-ahead arrives WITH an extra reply glued to it, in one
# write, before the handshake. Anyone on the path can do that -- the bytes are
# plaintext -- and the injected line is a valid answer to the step that runs
# AFTER the upgrade. Whether the probe treats it as one is the whole question:
# this peer sends nothing at all once TLS is up.
printf '%s\n' 'send "220 mail.test.local ESMTP\r\n"' \
	      'recv ehlo' \
	      'send "250-mail.test.local\r\n250 STARTTLS\r\n"' \
	      'recv starttls' \
	      'send "220 2.0.0 Ready to start TLS\r\n250 injected\r\n"' \
	      'starttls' \
	      'recv ehlo' \
	      'send "500 command not recognised\r\n"' \
	      'hangup'                                   > "$work/inject.script"

: > "$work/pids"
start_peer() {	# script portfile obsfile
	"$work/peer" "$1" "$3" "$work/cert.pem" "$work/key.pem" > "$2" &
	echo $! >> "$work/pids"
	i=0
	while [ "$i" -lt 60 ]; do [ -s "$2" ] && break; sleep 0.1; i=$((i + 1)); done
	cat "$2"
}
pgood=$(start_peer "$work/good.script" "$work/pg" "$work/og")
pbad=$(start_peer  "$work/bad.script"  "$work/pb" "$work/ob")
pinj=$(start_peer  "$work/inject.script" "$work/pi" "$work/oi")
register_cleanup "kill $(tr '\n' ' ' < "$work/pids") 2>/dev/null || :"
[ -n "$pgood" ] && [ -n "$pbad" ] || skip "a peer never named its port"

entry() {	# name port
	printf '[%s]\n   expect "220" until "220 "\n   send "ehlo xymonnet\\r\\n"\n   expect "250" until "250 "\n   send "starttls\\r\\n"\n   expect "220"\n   start tls\n   send "ehlo xymonnet\\r\\n"\n   expect "250"\n   options banner\n   port %s\n\n' "$1" "$2"
}
{ entry tlsok "$pgood"; entry tlsbad "$pbad"; entry tlsinj "$pinj"; } \
	> "$work/home/etc/protocols.cfg"
printf '127.0.0.1\tgood\t# tlsok\n127.0.0.1\tbad\t# tlsbad\n127.0.0.1\tinj\t# tlsinj\n' \
	> "$work/home/etc/hosts.cfg"

started=$(date +%s)
XYMONHOME="$work/home" "$XYMONNET" --no-update --noping --checkresponse=red \
	--dns=ip --timeout=30 >"$work/out.txt" 2>&1 || :
elapsed=$(( $(date +%s) - started ))

colour_of() { grep -oE "status\+[0-9]+ $1 (green|yellow|red|clear)" "$work/out.txt" | awk '{print $3}' | head -1; }

# 1. the conversation completes, over an upgraded connection
[ "$(colour_of good.tlsok)" = green ] || fail \
	"a server that upgraded properly was not green (got '$(colour_of good.tlsok)'):
$(grep -i tlsok "$work/out.txt" | head -4)
peer recorded: $(tr '\n' ' ' < "$work/og")"

# 2. the upgrade happened mid-conversation: an EHLO before it and one
#    after. A green without the second would mean the probe stopped at the
#    handshake and never spoke over the encrypted connection.
awk '/^tls-ok/{seen=1} /^got ehlo/{n++; if (seen) after=1} END{exit !(n >= 2 && after)}' "$work/og" || fail \
	"the second EHLO did not arrive after the upgrade, so nothing was sent
over the encrypted connection:
$(cat "$work/og")"

# 3. the certificate, read off the upgraded session -- which a plaintext
#    port has no other way to reach.
grep -qE "status\+[0-9]+ good\.sslcert " "$work/out.txt" || fail \
	"no sslcert status was sent for the upgraded connection, so the certificate
was never read:
$(grep -iE 'sslcert|tlsok' "$work/out.txt" | head -5)"

# 4. an upgrade that fails is not success
[ "$(colour_of bad.tlsbad)" != green ] || fail \
	"a server that agreed to STARTTLS and then failed the handshake was reported
GREEN. Everything before the handshake looked fine, which is exactly why this
has to be checked:
$(grep -i tlsbad "$work/out.txt" | head -4)"

# 5. plaintext does not cross the upgrade.
#
# Over TLS this server REFUSES the command. The only thing that can turn the
# entry green is the "250 injected" line glued to the go-ahead, which arrived
# in the clear and could have been written by anyone on the path.
#
# (The server has to say something after the upgrade for this to bite: a
# stranded buffer is only re-examined when a read arrives.)
# The peer must actually have upgraded, or "not green" proves nothing: a
# handshake that never happened is not green either.
grep -q '^tls-ok' "$work/oi" || fail \
	"the injecting peer never completed a handshake, so this case tested
nothing about what crosses the upgrade:
$(cat "$work/oi")"

[ "$(colour_of inj.tlsinj)" != green ] || fail \
	"a reply injected in PLAINTEXT before the handshake satisfied a step that
runs after it. Over TLS this server REFUSED the command, and the probe
reported the service healthy anyway -- on bytes that arrived in the clear and
could have been written by anyone on the path. This is the STARTTLS injection
of CVE-2011-0411; RFC 3207 4.2 requires the buffer to be discarded at the
upgrade:
$(grep -i tlsinj "$work/out.txt" | head -4)
peer recorded: $(tr '\n' ' ' < "$work/oi")"

# 6. a failed handshake is reported when it fails, not at the timeout.
#
# The timeout is 30s for three tests; anywhere near it means a dead
# connection was held open rather than reported.
[ "$elapsed" -lt 25 ] || fail \
	"the run took ${elapsed}s against a 30s timeout, so a failed handshake was
not reported when it failed -- the connection was held open until the clock
ran out. A step left current keeps the close guard from firing, and the
handshake is then retried on every pass:
$(grep -iE 'tlsbad|tlsinj' "$work/out.txt" | head -4)"

pass "a dialogue upgrades mid-conversation, speaks over the upgraded session, reaches the certificate, does not call a failed handshake success, does not carry plaintext across the upgrade, and fails when the handshake does"
