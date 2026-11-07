#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/xymonnet/tls-cert-name.sh
#
# An https test sends the URL's host as SNI and must then check that the
# certificate it gets back actually covers that name. A server reached at an
# address it does not hold -- an SNI that falls through to a default vhost, or
# a plain misconfiguration -- answers with a valid certificate issued for some
# other name; before this check that read green on "sslcert", because only the
# expiry, cipher and key size were scored.
#
# Pinned here, driven by the real xymonnet against an openssl s_server whose
# certificate names good.example.com (CN and SAN):
#   - the URL name good.example.com, forced to 127.0.0.1, matches the cert:
#     "Certificate covers 'good.example.com'", sslcert stays green;
#   - the URL name wrong.example.com, forced to the same server, does not:
#     "Certificate does NOT cover 'wrong.example.com'", sslcert turns red.
#
# The second case is the behaviour: reverting the "certnamematch == 0 -> red"
# line in send_sslcert_status() leaves it green and fails this test. The URL's
# "[name]:[port]=forcedIP" form (lib/url.c) keeps the name in the handshake and
# the Host: header while the socket goes to 127.0.0.1. Skips when xymonnet is
# not built, the openssl CLI is absent, or it cannot issue a SAN certificate
# (X509_check_host and -addext both need OpenSSL 1.0.2+/1.1.1+).

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin XYMONNET xymonnet/xymonnet
command -v openssl >/dev/null 2>&1 || skip "openssl CLI not available"

root=$(find_root)
work=$(mktempdir)
register_cleanup "rm -rf '$work'"

# Long-lived cert so expiry never colours the column -- the name match is
# scored in isolation. CN and SAN both name good.example.com.
openssl req -x509 -newkey rsa:2048 -keyout "$work/k.pem" -out "$work/c.pem" \
	-days 400 -nodes -subj "/CN=good.example.com" \
	-addext "subjectAltName=DNS:good.example.com" >/dev/null 2>&1 \
	|| skip "openssl could not issue a SAN test certificate"

# Minimal XYMONHOME.
mkdir -p "$work/home/etc" "$work/tmp"
cp "$root/xymonnet/protocols.cfg" "$work/home/etc/protocols.cfg"
export XYMONHOME="$work/home" XYMONTMP="$work/tmp"
export XYMONNETSVCS="http"

# run NAME EXPECT_COLOR EXPECT_LINE -- start an https server presenting the
# good.example.com cert, point one https test at it under the URL name NAME
# forced to 127.0.0.1, and check the sslcert colour and the cert-name line.
run() {
	name=$1; expect_color=$2; expect_line=$3
	port=$(( 20000 + ($$ % 20000) ))
	# -www answers the GET so the http test completes; -naccept covers retries.
	openssl s_server -accept "$port" -cert "$work/c.pem" -key "$work/k.pem" \
		-www -naccept 6 -quiet >/dev/null 2>&1 &
	srv=$!
	register_cleanup "stop_process $srv"
	i=0
	while ! (exec 3<>"/dev/tcp/127.0.0.1/$port") 2>/dev/null; do
		kill -0 "$srv" 2>/dev/null || skip "s_server exited before listening (port $port busy?)"
		i=$((i+1)); [ "$i" -gt 50 ] && skip "s_server never started listening"
		sleep 0.1
	done

	printf '127.0.0.1 host.example.com # https://%s:%s=127.0.0.1/\n' "$name" "$port" \
		> "$work/home/etc/hosts.cfg"
	out=$("$XYMONNET" --noping --no-update --timeout=6 2>/dev/null || :)
	stop_process "$srv"

	grep -qF "$expect_line" <<<"$out" \
		|| fail "$name: expected the cert-name line \"$expect_line\" in the sslcert status"
	grep -qE "sslcert $expect_color" <<<"$out" \
		|| fail "$name: expected sslcert $expect_color"
}

run good.example.com  green "Certificate covers 'good.example.com'"
run wrong.example.com red   "Certificate does NOT cover 'wrong.example.com'"

pass "xymonnet turns sslcert red when the certificate does not cover the name sent as SNI, and stays green when it does"
