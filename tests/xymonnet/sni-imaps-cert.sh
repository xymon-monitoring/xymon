#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/xymonnet/sni-imaps-cert.sh
#
# imaps completes the TLS handshake and then scores its "sslcert" column on the
# two things that column carries: whether the certificate covers the name the
# test sent as SNI, and how soon it expires. The companion sni-imaps.sh checks
# only the name put on the wire, with a peer that reads the ClientHello and
# stops; here the handshake and the imaps dialogue both run to completion, so a
# real certificate reaches the column and both checks are exercised.
#
# Driven by the real xymonnet against dialogue-peer, which upgrades to TLS with
# a generated certificate (CN and SAN good.example.com) and speaks the imaps
# dialogue through to BYE. The peer records the line it read, so the test can
# assert the client reached LOGOUT over TLS -- i.e. the handshake completed.
#
#   - imaps://good.example.com, long-lived cert: covers the name and is not
#     expiring, sslcert green;
#   - imaps://wrong.example.com, same cert: "does NOT cover", sslcert red;
#   - imaps://good.example.com, cert expiring in 3 days (< sslalarmdays=7): the
#     name still covers, but "expires in ... days" turns sslcert red;
#   - imaps://0.0.0.0, an address, sends no SNI: the name check is skipped --
#     neither "covers" nor "does NOT cover" -- and only the expiry is scored,
#     here green. Reverting the imaps servername hook-up leaves the handshake
#     nameless and fails the first case.
#
# Skips when xymonnet, the C compiler or the openssl CLI is absent, when
# dialogue-peer will not link against libssl, or when openssl cannot issue a
# SAN certificate (X509_check_host and -addext need OpenSSL 1.0.2+/1.1.1+).

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

root=$(find_root)
require_bin XYMONNET xymonnet/xymonnet
: "${CC:=cc}"
command -v "$CC" >/dev/null 2>&1 || skip "no C compiler available (CC=$CC)"
command -v openssl >/dev/null 2>&1 || skip "openssl CLI not available"

work=$(mktempdir); register_cleanup "rm -rf '$work'"
mkdir -p "$work/home/etc" "$work/tmp"
cp "$root/xymonnet/protocols.cfg" "$work/home/etc/protocols.cfg"
export XYMONHOME="$work/home" XYMONTMP="$work/tmp"
export XYMONNETSVCS="imaps"

# dialogue-peer, the scriptable TLS peer.
# shellcheck disable=SC2046  # flag lists, split on purpose
"$CC" $(xymon_cflags "$root") $(xymon_sslcflags "$root") -o "$work/peer" \
	"$root/tests/lib/dialogue-peer.c" $(xymon_ldflags "$root") -lssl -lcrypto 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; skip "dialogue-peer does not compile against libssl"; }

# gencert BASE DAYS -- a self-signed cert naming good.example.com (CN and SAN),
# valid for DAYS. A long life scores green on expiry, a short one red.
gencert() {
	openssl req -x509 -newkey rsa:2048 -keyout "$1.key" -out "$1.crt" \
		-days "$2" -nodes -subj "/CN=good.example.com" \
		-addext "subjectAltName=DNS:good.example.com" >/dev/null 2>&1
}
gencert "$work/good" 400 || skip "openssl could not issue a SAN test certificate"
gencert "$work/soon" 3   || skip "openssl could not issue a SAN test certificate"

# imaps is TLS from the first byte (protocols.cfg "start tls"): upgrade, greet,
# read the LOGOUT, say BYE.
printf '%s\n' 'starttls' \
	      'send "* OK IMAP4 ready\r\n"' \
	      'recv ABC123 LOGOUT' \
	      'send "* BYE logging out\r\n"' \
	      'hangup'                          > "$work/imaps.script"

# run SNINAME CERTBASE COLOR [LINE|!LINE ...] -- start the peer presenting
# CERTBASE, point one "imaps://SNINAME:PORT" test at it (the host forced to
# 127.0.0.1), and check: the client reached LOGOUT over TLS, the sslcert colour
# is COLOR, each LINE is present and each !LINE is absent.
run() {
	sni=$1; certbase=$2; color=$3; shift 3
	: > "$work/obs"; : > "$work/port"
	"$work/peer" "$work/imaps.script" "$work/obs" "$certbase.crt" "$certbase.key" > "$work/port" &
	srv=$!; register_cleanup "stop_process $srv"
	i=0
	while [ ! -s "$work/port" ]; do
		kill -0 "$srv" 2>/dev/null || skip "dialogue-peer exited before listening"
		i=$((i + 1)); [ "$i" -gt 60 ] && skip "dialogue-peer never started listening"
		sleep 0.1
	done
	port=$(cat "$work/port")

	printf '127.0.0.1 host.example.com # imaps://%s:%s\n' "$sni" "$port" > "$work/home/etc/hosts.cfg"
	out=$("$XYMONNET" --noping --no-update --timeout=6 2>/dev/null || :)
	stop_process "$srv"

	grep -qF 'ABC123 LOGOUT' "$work/obs" \
		|| fail "$sni/$certbase: the client never reached LOGOUT over TLS (handshake failed?)"
	grep -qE "sslcert $color" <<<"$out" \
		|| fail "$sni/$certbase: expected sslcert $color; sslcert line: $(first_line "$(grep -i 'sslcert' <<<"$out" || :)")"
	for want in "$@"; do
		case "$want" in
		'!'*)	grep -qF "${want#!}" <<<"$out" \
				&& fail "$sni/$certbase: did NOT expect \"${want#!}\" in the sslcert status" || : ;;
		*)	grep -qF "$want" <<<"$out" \
				|| fail "$sni/$certbase: expected \"$want\" in the sslcert status" ;;
		esac
	done
}

# name and expiry both pass
run good.example.com  "$work/good" green "Certificate covers 'good.example.com'"
# name fails
run wrong.example.com "$work/good" red  "Certificate does NOT cover 'wrong.example.com'"
# name passes, expiry fails
run good.example.com  "$work/soon" red  "Certificate covers 'good.example.com'"
# an address sends no SNI: the name check is skipped, only expiry is scored
run 0.0.0.0           "$work/good" green "!Certificate covers" "!Certificate does NOT cover"

pass "imaps completes the handshake and scores sslcert on the SNI name and the expiry, and an address skips the name check"
