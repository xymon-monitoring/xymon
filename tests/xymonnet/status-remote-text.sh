#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# The status page shows status text as HTML: that is how "&red" draws a
# colour icon and how a status can carry a link. Text that xymonnet copies
# from a remote service is not meant that way: an SMTP banner naming its
# host as "<mail.example.com>", an HTTP "Link: <https://...>" header, a
# reason phrase, or an "O=" holding a "<" or "&". Written as it came, the
# page read it as markup, so the "<...>" part vanished and an "&red" in it
# drew an icon.
#
# This checks that xymonnet writes such text with & < > " ' as entities:
#   - a TCP service banner;
#   - the HTTP reason phrase (the status's first line) and the headers;
#   - the subject in the sslcert status.
set -euo pipefail
. "$(dirname "$0")/../lib/assert.sh"

root=$(find_root)
require_bin XYMONNET xymonnet/xymonnet
require_cc
command -v openssl >/dev/null 2>&1 || skip "openssl CLI not available"

work=$(mktempdir); register_cleanup "rm -rf '$work'"
mkdir -p "$work/home/etc" "$work/tmp"
export XYMONTMP="$work/tmp"

"$CC" $(xymon_cflags "$root") $(xymon_sslcflags "$root") -o "$work/peer" \
	"$root/tests/lib/dialogue-peer.c" $(xymon_ldflags "$root") -lssl -lcrypto 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; skip "dialogue-peer does not compile"; }

register_cleanup "stop_process \$(cat '$work/pids' 2>/dev/null)"
# startpeer NAME SCRIPT [CERT KEY]: start a peer, leave its port in $work/port.NAME
startpeer() {
	n=$1; shift
	"$work/peer" "$1" "$work/obs.$n" ${2:+"$2"} ${3:+"$3"} > "$work/port.$n" &
	echo $! >> "$work/pids"
	i=0
	while [ ! -s "$work/port.$n" ]; do
		i=$((i + 1)); [ "$i" -gt 60 ] && skip "dialogue-peer never started listening"
		sleep 0.1
	done
}

# A banner, an HTTP answer with a Link header, and one with a reason phrase
printf 'recvany\nsend "220 <mail.example.com> ESMTP ready &red\\r\\n"\nhold 4\n' > "$work/banner"
printf 'recvany\nsend "HTTP/1.0 200 OK\\r\\nLink: <https://example.com/style.css>; rel=preload\\r\\nContent-Length: 2\\r\\n\\r\\nok"\nhold 4\n' > "$work/link"
printf 'recvany\nsend "HTTP/1.0 503 Busy <try later>\\r\\nContent-Length: 2\\r\\n\\r\\nok"\nhold 4\n' > "$work/busy"
startpeer banner "$work/banner"
startpeer link "$work/link"
startpeer busy "$work/busy"

# A TLS service whose certificate's O= holds a "<" and an "&"
openssl req -x509 -newkey rsa:2048 -keyout "$work/c.key" -out "$work/c.crt" \
	-days 400 -nodes -subj "/CN=good.example.com/O=Example <Ops> & Co" \
	-addext "subjectAltName=DNS:good.example.com" >/dev/null 2>&1 \
	|| skip "openssl could not issue a test certificate"
printf '%s\n' 'starttls' 'send "* OK IMAP4 ready\r\n"' 'recv ABC123 LOGOUT' \
	'send "* BYE logging out\r\n"' 'hangup' > "$work/imaps"
startpeer tls "$work/imaps" "$work/c.crt" "$work/c.key"

printf '127.0.0.1\thost.example.com\t# t1 http://127.0.0.1:%s/ http://127.0.0.1:%s/busy\n' \
	"$(cat "$work/port.link")" "$(cat "$work/port.busy")" > "$work/home/etc/hosts.cfg"
cp "$root/xymonnet/protocols.cfg" "$work/home/etc/protocols.cfg"
printf '[t1]\n   send "probe\\r\\n"\n   expect "220"\n   port %s\n' "$(cat "$work/port.banner")" >> "$work/home/etc/protocols.cfg"

XYMONHOME="$work/home" XYMONNETSVCS="t1" "$XYMONNET" --no-update --noping \
	--checkresponse --dns=ip --timeout=8 >"$work/out" 2>&1 || :
# The certificate is reported without --checkresponse, as sni-imaps-cert.sh does
mkdir -p "$work/home2/etc"
cp "$root/xymonnet/protocols.cfg" "$work/home2/etc/protocols.cfg"
printf '127.0.0.1 host.example.com # imaps://good.example.com:%s\n' "$(cat "$work/port.tls")" > "$work/home2/etc/hosts.cfg"
XYMONHOME="$work/home2" XYMONNETSVCS="imaps" "$XYMONNET" --no-update --noping \
	--timeout=8 >>"$work/out" 2>&1 || :

check() {
	what=$1 want=$2 raw=$3
	grep -aqF -- "$want" "$work/out" || fail "$what is not shown as text: no '$want' in
$(head -40 "$work/out")"
	! grep -aqF -- "$raw" "$work/out" || fail "$what went into the status as markup: '$raw'"
}
check "the banner" "220 &lt;mail.example.com&gt; ESMTP ready &amp;red" "<mail.example.com>"
check "the Link header" "Link: &lt;https://example.com/style.css&gt;; rel=preload" "<https://example.com/style.css>"
check "the reason phrase" "Busy &lt;try later&gt;" "<try later>"
# The subject's form depends on the OpenSSL call that writes it; the "<" is
# an entity either way.
check "the certificate subject" "&lt;Ops" "<Ops>"

pass "xymonnet shows a banner, HTTP headers, a reason phrase and a certificate subject as text"
