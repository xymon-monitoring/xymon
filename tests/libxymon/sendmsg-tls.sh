#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/libxymon/sendmsg-tls.sh
#
# lib/sendmsg.c: a xymons://host[:port] recipient is reached over TLS, on
# XYMONDTLSPORT unless a port is given. A build without OpenSSL refuses
# such a recipient before connecting, and that is what is checked there.
#
# The xymon client talks to a TLS stand-in (sendmsg-tls-harness.c) with the
# test PKI in tests/fixtures/tls. Checked:
#
#   - the message arrives whole, ended by close_notify, and the reply is
#     printed -- on 127.0.0.1, on XYMONDTLSPORT, on [::1];
#   - the certificate must name the host connected to, or XYMON_TLS_SNI
#     when set; without XYMON_TLS_CA the system's trust store is used, which
#     does not know the test CA; XYMON_TLS_VERIFY=none connects anyway;
#   - XYMON_TLS_VERIFY=peer pins: it trusts a self-signed certificate that
#     XYMON_TLS_CA holds, whatever name it is asked for, where full refuses
#     the same certificate for a name it does not carry;
#   - XYMON_TLS_CERT and _KEY present a client certificate to a server
#     that requires one;
#   - a message counts as sent only when the server answers with
#     close_notify, even when no reply is wanted: a server that refuses the
#     client's certificate -- which under TLS 1.3 it does after the client's
#     side of the handshake -- or hangs up without one fails the send;
#   - a peer that does not speak TLS gets a TLS handshake, never the
#     message in plaintext.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
require_cc
require_bin XYMON common/xymon
sslcflags=$(xymon_sslcflags "$ROOT")
case $sslcflags in
*HAVE_OPENSSL*) ;;
*)	got=$(XYMON_TIMEOUT=5 "$XYMON" "xymons://127.0.0.1:1" "query sendmsg.tls" 2>&1 || true)
	assert_contains "this build has no TLS support" "$got" "a build without OpenSSL did not refuse a xymons:// recipient"
	pass "a build without OpenSSL refuses a xymons:// recipient" ;;
esac
# A server build has libxymoncomm, holding all of libxymon; a client build
# has libxymonclientcomm, with the rest of what sendmessage() needs in
# libxymonclient.
commlib=$ROOT/lib/libxymoncomm.a
restlib=
if [ ! -f "$commlib" ]; then
	commlib=$ROOT/lib/libxymonclientcomm.a
	restlib=$ROOT/lib/libxymonclient.a
fi
[ -f "$commlib" ] || skip "neither lib/libxymoncomm.a nor lib/libxymonclientcomm.a is built"

work=$(mktempdir)
cp "$ROOT"/tests/fixtures/tls/*.pem "$ROOT"/tests/fixtures/tls/*.key "$work/"
chmod 600 "$work"/*.key
pids=""
register_cleanup 'for p in $pids; do kill "$p" 2>/dev/null || true; done'

# shellcheck disable=SC2046,SC2086  # flag lists, split on purpose
"$CC" $(xymon_cflags "$ROOT") $sslcflags -iquote "$ROOT/lib" -o "$work/harness" \
	"$(dirname "$0")/sendmsg-tls-harness.c" "$commlib" ${restlib:+"$restlib"} \
	$(xymon_ldflags "$ROOT") -lssl -lcrypto 2>"$work/cc.log" \
	|| fail "the harness does not build: $(cat "$work/cc.log")"

# serve ADDRESS MODE [CERT] -- one stand-in connection, with CERT's
# certificate (server.pem unless given); its port in $port, what it saw in
# $work/seen. Returns 1 when ADDRESS cannot be bound here.
serve() {
	local i
	: >"$work/port"
	"$work/harness" "$work" "$1" "$2" ${3:+"$3"} >"$work/port" 2>"$work/seen" &
	hpid=$!
	pids="$pids $hpid"
	for i in $(seq 1 50); do
		[ -s "$work/port" ] && break
		kill -0 "$hpid" 2>/dev/null || return 1
		sleep 0.1
	done
	[ -s "$work/port" ] || return 1
	port=$(cat "$work/port")
}

# send RECIPIENT [VAR=VALUE...] -- what the client printed, stderr included;
# then waits for the stand-in, so $work/seen is complete. MSG is the message,
# a query (which wants a reply) unless set.
send() {
	local r=$1 out
	shift
	out=$(env XYMON_TIMEOUT=5 XYMON_TLS_CA="$work/ca.pem" "$@" "$XYMON" "$r" "${MSG:-query sendmsg.tls}" 2>&1 || true)
	wait "$hpid" 2>/dev/null || true
	printf '%s' "$out"
}

seen() { cat "$work/seen"; }

serve 127.0.0.1 tls || fail "the stand-in cannot listen on 127.0.0.1: $(seen)"
got=$(send "xymons://127.0.0.1:$port")
assert_contains "reply to: query sendmsg.tls" "$got" "no reply over TLS from 127.0.0.1:$port ($(seen))"
assert_contains "message: query sendmsg.tls" "$(seen)" "the stand-in did not get the message"
assert_contains "close_notify: yes" "$(seen)" "the message was not ended by close_notify"

serve 127.0.0.1 tls
got=$(send "xymons://localhost" XYMONDTLSPORT="$port")
assert_contains "reply to: query sendmsg.tls" "$got" "xymons://localhost did not use XYMONDTLSPORT $port ($(seen))"

serve 127.0.0.1 tls
got=$(send "xymons://127.0.0.1:$port" XYMON_TLS_SNI=localhost)
assert_contains "reply to:" "$got" "XYMON_TLS_SNI=localhost, a name the certificate carries, was refused ($(seen))"

serve 127.0.0.1 tls
got=$(send "xymons://127.0.0.1:$port" XYMON_TLS_SNI=wrong.example.com)
assert_not_contains "reply to:" "$got" "a certificate that does not name XYMON_TLS_SNI was accepted"
assert_contains "TLS handshake with Xymon daemon" "$got" "the refusal does not say the TLS handshake failed"

serve 127.0.0.1 tls
got=$(send "xymons://127.0.0.1:$port" XYMON_TLS_CA=)
assert_not_contains "reply to:" "$got" "without XYMON_TLS_CA, a certificate the system's trust store does not know was accepted"

serve 127.0.0.1 tls
got=$(send "xymons://127.0.0.1:$port" XYMON_TLS_CA= XYMON_TLS_VERIFY=none)
assert_contains "reply to:" "$got" "XYMON_TLS_VERIFY=none did not connect ($(seen))"

# Pinning: renewed.pem is self-signed, so XYMON_TLS_CA holding it trusts it
# alone; it names localhost and 127.0.0.1, not the name asked for here
serve 127.0.0.1 tls renewed
got=$(send "xymons://127.0.0.1:$port" XYMON_TLS_CA="$work/renewed.pem" XYMON_TLS_VERIFY=peer XYMON_TLS_SNI=not-its-name.example.com)
assert_contains "reply to:" "$got" "XYMON_TLS_VERIFY=peer refused a pinned certificate ($(seen))"
serve 127.0.0.1 tls renewed
got=$(send "xymons://127.0.0.1:$port" XYMON_TLS_CA="$work/renewed.pem" XYMON_TLS_SNI=not-its-name.example.com)
assert_not_contains "reply to:" "$got" "XYMON_TLS_VERIFY=full accepted a certificate for a name it does not carry"

serve 127.0.0.1 mtls
got=$(send "xymons://127.0.0.1:$port")
assert_not_contains "reply to:" "$got" "a server requiring a client certificate answered a client without one"
assert_contains "handshake: refused" "$(seen)" "the stand-in accepted a client without a certificate"

serve 127.0.0.1 mtls
got=$(MSG="status sendmsg.tls green no certificate" send "xymons://127.0.0.1:$port")
assert_contains "did not take the message over TLS" "$got" \
	"a status refused for its missing certificate looked sent ($(seen))"

serve 127.0.0.1 tls
got=$(MSG="status sendmsg.tls green taken" send "xymons://127.0.0.1:$port")
assert_contains "message: status sendmsg.tls green taken" "$(seen)" "the stand-in did not get the status"
assert_not_contains "did not take" "$got" "a status the server took, ending with close_notify, was reported as failed"

# A program that wants no reply, which is how a status is usually sent
serve 127.0.0.1 mtls
got=$(XYMON_TLS_CA="$work/ca.pem" "$work/harness" send "xymons://127.0.0.1:$port" "status sendmsg.tls green no reply wanted" 2>&1 || true)
wait "$hpid" 2>/dev/null || true
assert_not_contains "result: 0" "$got" "a status sent without a reply buffer, refused for its missing certificate, returned XYMONSEND_OK"
assert_contains "did not take the message over TLS" "$got" "the refusal of a status sent without a reply buffer was not reported"
serve 127.0.0.1 tls
got=$(XYMON_TLS_CA="$work/ca.pem" "$work/harness" send "xymons://127.0.0.1:$port" "status sendmsg.tls green no reply wanted" 2>&1 || true)
wait "$hpid" 2>/dev/null || true
assert_contains "result: 0" "$got" "a status sent without a reply buffer, and taken, did not return XYMONSEND_OK"

serve 127.0.0.1 cut
got=$(MSG="status sendmsg.tls green cut off" send "xymons://127.0.0.1:$port")
assert_contains "did not take the message over TLS" "$got" "a server that hung up without close_notify was taken to have the message"

serve 127.0.0.1 mtls
got=$(send "xymons://127.0.0.1:$port" XYMON_TLS_CERT="$work/web01.pem" XYMON_TLS_KEY="$work/web01.key")
assert_contains "reply to:" "$got" "the client certificate was not presented ($(seen))"
assert_contains "client web01.example.com: 1" "$(seen)" "the stand-in did not see web01's certificate"

serve 127.0.0.1 plain
got=$(send "xymons://127.0.0.1:$port" XYMON_TIMEOUT=2)
assert_contains "first byte: 0x16" "$(seen)" "a peer that does not speak TLS did not get a TLS handshake"

serve ::1 tls || pass_partial "xymons:// reaches a TLS server on IPv4, by address and by name" \
	"IPv6: this host has no IPv6 loopback, ::1 cannot be bound ($(seen))"
got=$(send "xymons://[::1]:$port")
assert_contains "reply to: query sendmsg.tls" "$got" "no reply over TLS from [::1]:$port ($(seen))"

pass "xymons:// sends over TLS with close_notify, verifies the name or XYMON_TLS_SNI, presents a client certificate, and never falls back to plaintext"
