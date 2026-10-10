#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/xymontls.sh
#
# lib/xymontls.c: the TLS contexts xymond and its clients will use, and the
# identity a verified certificate carries. Filed under server: only a server
# build compiles the library with OpenSSL (configure.client does not ask for it).
#
# Each case is a real handshake (tests/server/xymontls-harness.c) with the
# test PKI in tests/fixtures/tls: a CA, a server certificate for localhost,
# 127.0.0.1 and ::1, a client certificate whose only name is
# web01.example.com in its subjectAltName, and a self-signed "rogue" naming
# the same. Checked:
#
#   - a client in full verification accepts the server for a name or address
#     the certificate carries, and refuses it for any other;
#   - without a CA file the client uses the system's trust store, which does
#     not know the test CA; with no verification it connects anyway;
#   - a server that requires client certificates refuses a client without
#     one, and one the CA did not issue, and accepts web01;
#   - the identity is the subjectAltName, letter case aside, never the CN;
#   - a private key that others may read is refused, as is a key that does
#     not match its certificate.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
require_cc
[ -f "$ROOT/lib/libxymoncomm.a" ] || skip "lib/libxymoncomm.a is not built"
sslcflags=$(xymon_sslcflags "$ROOT")
case $sslcflags in *HAVE_OPENSSL*) ;; *) skip "this build has no OpenSSL" ;; esac

work=$(mktempdir)
cp "$ROOT"/tests/fixtures/tls/*.pem "$ROOT"/tests/fixtures/tls/*.key "$work/"
chmod 600 "$work"/*.key

# shellcheck disable=SC2046
"$CC" $(xymon_cflags "$ROOT") $sslcflags -iquote "$ROOT/lib" -o "$work/harness" \
	"$(dirname "$0")/xymontls-harness.c" "$ROOT/lib/libxymoncomm.a" \
	$(xymon_ldflags "$ROOT") -lssl -lcrypto 2>"$work/cc.log" \
	|| fail "the harness does not build: $(cat "$work/cc.log")"

# expect CASE LINE... -- every LINE is in what the harness printed for CASE
expect() {
	local c=$1 out line
	shift
	out=$("$work/harness" "$work" "$c" 2>&1) || fail "case $c did not run: $out"
	for line in "$@"; do
		assert_contains "$line" "$out" "case $c"
	done
}

expect full-name        "server: ok" "client: ok"
expect full-ipv4        "client: ok"
expect full-ipv6        "client: ok"
expect full-wrong-name  "client: refused"
expect full-other-ip    "client: refused"
expect peer-system-store "client: refused"
expect none-system-store "client: ok"

expect mtls-nocert      "server: refused"
expect mtls-rogue       "server: refused"
expect mtls-web01       "server: ok" "client: ok" \
	"name web01.example.com: 1" "name WEB01.Example.COM: 1" \
	"name other.example.com: 0" "name not-the-name-checked: 0"
expect mtls-optional    "server: ok" "name web01.example.com: 1"

# The private key's permissions, and whether it belongs to its certificate
cp "$work/server.key" "$work/open.key"
chmod 644 "$work/open.key"
got=$("$work/harness" ctx-error "$work/server.pem" "$work/open.key")
assert_contains "may be read by anyone" "$got" "a private key that others may read was accepted"
got=$("$work/harness" ctx-error "$work/server.pem" "$work/server.key")
[ "$got" = ok ] || fail "a private key readable by its owner only was refused: $got"
got=$("$work/harness" ctx-error "$work/server.pem" "$work/web01.key")
[ "$got" != ok ] || fail "a key that does not belong to its certificate was accepted"
assert_contains "web01.key" "$got" "the refusal of a mismatched key does not name the key"

pass "xymontls verifies a server by name or address, falls back on the system's trust store, requires and verifies client certificates, takes a client's identity from its subjectAltName only, and refuses an exposed or mismatched key"
