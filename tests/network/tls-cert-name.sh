#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/network/tls-cert-name.sh
#
# xymonnet checks whether the certificate a TLS service presents actually
# covers the name it was asked for (the SNI/host name), via X509_check_host,
# and reports it in the sslcert status. This is informational: it does NOT
# change the sslcert colour, which stays driven by expiry and cipher strength.
#
# Static assertions guard the wiring. When the built xymonnet and the openssl
# CLI are both present, a behavioural pass drives xymonnet against a real
# openssl s_server whose certificate names a known host, and checks both the
# match/mismatch report and that a mismatch leaves the column colour alone.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
cc="$ROOT/xymonnet/contest.c"
nc="$ROOT/xymonnet/xymonnet.c"
[ -f "$cc" ] && [ -f "$nc" ] || skip "xymonnet sources not in this checkout"
has() { grep -qE -- "$1" "$2"; }

# --- static wiring guards (always run) ---
has 'X509_check_host' "$cc" \
	|| fail "contest.c does not call X509_check_host to match the cert name"
has 'tt->checkname = tt->sni' "$nc" \
	|| fail "the SNI name is not handed to the cert name check"
has 'certnamematch = testresult->certnamematch' "$nc" \
	|| fail "the cert name-match result is not propagated to the test item"
has 'Certificate does NOT cover' "$nc" \
	|| fail "a cert name mismatch is not reported"
has 'Certificate covers' "$nc" \
	|| fail "a cert name match is not reported"

# --- behavioural pass (needs the built binary + the openssl CLI) ---
require_bin XYMONNET xymonnet/xymonnet
command -v openssl >/dev/null 2>&1 || skip "openssl CLI not available for the behavioural pass"

work=$(mktempdir); register_cleanup "rm -rf '$work'"
mkdir -p "$work/home/etc" "$work/tmp"
cp "$ROOT/xymonnet/protocols.cfg" "$work/home/etc/protocols.cfg"

# Long-lived cert so expiry never colours the column -- we are checking the
# name match in isolation. CN and SAN both name good.example.com.
openssl req -x509 -newkey rsa:2048 -keyout "$work/k.pem" -out "$work/c.pem" -days 400 -nodes \
	-subj "/CN=good.example.com" -addext "subjectAltName=DNS:good.example.com" >/dev/null 2>&1 \
	|| skip "openssl could not generate a test certificate"

port=12744
openssl s_server -accept "$port" -cert "$work/c.pem" -key "$work/k.pem" -quiet -naccept 8 >/dev/null 2>&1 &
srv=$!; register_cleanup "kill $srv 2>/dev/null || :"
i=0
while ! (exec 3<>"/dev/tcp/127.0.0.1/$port") 2>/dev/null; do
	kill -0 "$srv" 2>/dev/null || fail "s_server exited before listening"
	i=$((i+1)); [ "$i" -gt 50 ] && fail "s_server never started listening"
	sleep 0.1
done
exec 3>&- 3<&- 2>/dev/null || :

netrun() {	# NAME -> xymonnet --no-update output (status + cert detail)
	printf '127.0.0.1 %s # imaps:%s\n' "$1" "$port" > "$work/home/etc/hosts.cfg"
	( export XYMONHOME="$work/home" XYMONTMP="$work/tmp" HOSTSCFG="$work/home/etc/hosts.cfg" \
	    XYMONNETSVCS="imaps" MACHINE=t CONNTEST=FALSE PINGCOLUMN=conn FPING=/x NTPDATE=/x \
	    TRACEROUTE=/x RPCINFO=/x NONETPAGE="" NETFAILTEXT=no XYMONROUTERTEXT=r TASKSLEEP=300 \
	    BBLOCATION="" XYMONNETWORK=""
	  "$XYMONNET" --noping --no-update --dns=ip --timeout=6 2>/dev/null )
}

out=$(netrun good.example.com)
grep -qF "Certificate covers 'good.example.com'" <<<"$out" \
	|| fail "a matching cert is not reported as covering the name"
grep -qE "sslcert green" <<<"$out" \
	|| fail "a matching, unexpired cert should leave sslcert green"

out=$(netrun wrong.example.com)
grep -qF "Certificate does NOT cover 'wrong.example.com'" <<<"$out" \
	|| fail "a mismatched cert is not reported"
grep -qE "sslcert green" <<<"$out" \
	|| fail "a name mismatch must not change the sslcert colour (it is informational)"

pass "xymonnet reports whether the cert covers the SNI name, without changing the sslcert colour"
