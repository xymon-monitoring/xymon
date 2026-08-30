#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# What a send can compute.
#
# ${md5:} and ${base64:} between them reach APOP and SASL PLAIN, and stop
# there. CRAM-MD5 is an HMAC, not a hash; MySQL's login is a SHA-1 chain;
# the SCRAM family that PostgreSQL, MongoDB and AMQP use is HMAC-SHA-256.
# None of those is expressible by nesting a bare hash, however deeply -- so
# a probe could reach the greeting of those services and never past it.
#
# The digests come from lib/digest.c, which already implemented every one
# of them, and the HMAC is RFC 2104 over the same. ${hex:} and ${len:} are
# the two primitives the rest kept wanting: a value the file cannot count
# for itself, and a value it cannot spell.
#
# The probe computes, sends, and the peer records the line verbatim. The
# expected values come from OPENSSL, not from us -- a peer that recomputed
# them with the code under test would pass on any answer at all.
#
# THE CONTROL is [wrongkey]: the same HMAC over a different key. It must
# NOT match the value openssl derives for the right one, or the key is not
# reaching the digest and every assertion above is passing on a constant.

set -eu
. "$(dirname "$0")/../lib/assert.sh"
root=$(find_root)

require_bin XYMONNET xymonnet/xymonnet
require_cc
command -v openssl >/dev/null 2>&1 || skip "openssl(1) is needed for the reference values"

work=$(mktempdir); register_cleanup "rm -rf '$work'"
mkdir -p "$work/home/etc"

"$CC" -o "$work/peer" "$root/tests/lib/dialogue-peer.c" -lssl -lcrypto 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; skip "dialogue-peer does not compile"; }

# The reference values, from a different implementation than the one we test.
hex_of() { printf '%s' "$2" | openssl dgst -"$1" -r | cut -d' ' -f1; }
mac_of() { printf '%s' "$3" | openssl dgst -"$1" -hmac "$2" -r | cut -d' ' -f1; }

w_sha1=$(hex_of sha1   abc)
w_sha256=$(hex_of sha256 abc)
w_sha512=$(hex_of sha512 abc)
w_hmd5=$(mac_of md5    s3cret msg)
w_hsha1=$(mac_of sha1  s3cret msg)
w_hsha256=$(mac_of sha256 s3cret msg)
w_wrong=$(mac_of sha256 OTHERKEY msg)
w_b64=$(printf '%s' abc | openssl base64)

printf 'mycred\ts3cret\ts3cret\n' > "$work/home/etc/credentials.cfg"
chmod 600 "$work/home/etc/credentials.cfg"

printf '%s\n' 'send "+OK ready\r\n"' 'recvany' 'send "+OK\r\n"' 'hold 5' > "$work/one.script"

: > "$work/pids"
start() {	# script portfile obsfile
	"$work/peer" "$1" "$3" > "$2" &
	echo $! >> "$work/pids"
	local i=0
	while [ "$i" -lt 50 ]; do [ -s "$2" ] && break; sleep 0.1; i=$((i + 1)); done
	cat "$2"
}
pd=$(start "$work/one.script" "$work/pd" "$work/od")
pw=$(start "$work/one.script" "$work/pw" "$work/ow")
register_cleanup "kill $(tr '\n' ' ' < "$work/pids") 2>/dev/null || :"
[ -n "$pd" ] && [ -n "$pw" ] || fail "a peer never named its port"

cat > "$work/home/etc/protocols.cfg" <<CFG
[expansions]
   options banner
   port $pd

   state greeting
      credentials mycred
      timeout(10)                 -> fail
      expect "+OK"                -> compute

   state compute
      send "V \${sha1:abc} \${sha256:abc} \${sha512:abc} \${hmac-md5:\${password},msg} \${hmac-sha1:\${password},msg} \${hmac-sha256:\${password},msg} \${hex:abc} \${len:abc} \${base64:abc}\r\n"
      timeout(10)                 -> fail
      expect "+OK"                -> success

[wrongkey]
   options banner
   port $pw

   state greeting
      timeout(10)                 -> fail
      expect "+OK"                -> compute

   state compute
      send "V \${hmac-sha256:OTHERKEY,msg}\r\n"
      timeout(10)                 -> fail
      expect "+OK"                -> success
CFG
printf '127.0.0.1\tex\t# expansions\n127.0.0.1\twk\t# wrongkey\n' > "$work/home/etc/hosts.cfg"

XYMONHOME="$work/home" "$XYMONNET" --no-update --noping --checkresponse=red \
	--dns=ip --timeout=30 >"$work/out.txt" 2>&1 || :
sleep 1

grep -qi "unknown expansion" "$work/out.txt" && fail \
	"a function the driver implements was refused when the file was read. The
list that validates a name and the list that runs it have drifted apart:
$(grep -i 'unknown expansion' "$work/out.txt" | head -3)"

want="V $w_sha1 $w_sha256 $w_sha512 $w_hmd5 $w_hsha1 $w_hsha256 616263 3 $w_b64"
got=$(grep '^got V ' "$work/od" | head -1 | sed 's/^got //')

[ "$got" = "$want" ] || fail \
	"a computed value did not match the one openssl derives.
 want: $want
 got:  $got
peer saw:
$(cat "$work/od")"

colour_of() { grep -oE "status\+[0-9]+ $1 (green|yellow|red|clear)" "$work/out.txt" | awk '{print $3}' | head -1; }
[ "$(colour_of ex.expansions)" = green ] || fail \
	"the entry did not complete:
$(grep -i expansions "$work/out.txt" | head -4)"

# THE CONTROL: a different key must give a different digest
gotw=$(grep '^got V ' "$work/ow" | head -1 | sed 's/^got V //')
[ "$gotw" = "$w_wrong" ] || fail \
	"HMAC over a second key did not match openssl for that key either:
 want: $w_wrong
 got:  $gotw"
[ "$gotw" = "$w_hsha256" ] && fail \
	"two different keys produced the same HMAC, so the key is not reaching the
digest and every assertion above is passing on a constant"

pass "sha1/224/256/384/512, hmac-*, hex, len and base64 produce what openssl does"
