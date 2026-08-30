#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# CRAM-MD5, end to end, because it is the shape a hash cannot reach.
#
# RFC 2195: the server sends a challenge in base64, and the client answers
# base64(USER + " " + hex(HMAC-MD5(SECRET, challenge))). Three things have
# to be true at once, and none of them was, before:
#
#   the challenge must be DECODED before it is hashed -- ${unbase64:}
#   the digest must be KEYED -- ${hmac-md5:}, which no nesting of ${md5:}
#     produces, because an HMAC is two hashes over a padded key
#   the result must be re-encoded with the username -- ${base64:}
#
# So this is the smallest exchange that proves the value layer reaches a
# real authentication rather than an APOP-shaped one, and it is written the
# way a site would write it: one state per turn, credentials from the
# store, nothing about the algorithm in the file except its name.
#
# THE PEER COMPUTES THE ANSWER ITSELF, with OpenSSL's HMAC and its own
# base64. It never sees ours except to compare, so a wrong digest fails
# here -- a peer that recorded what arrived would pass on anything.
#
# THE CONTROL is [cramwrong]: the same exchange with the wrong secret. It
# must NOT be accepted, or the peer is not checking and the assertion above
# is decoration.

set -eu
. "$(dirname "$0")/../lib/assert.sh"
root=$(find_root)

require_bin XYMONNET xymonnet/xymonnet
require_cc

work=$(mktempdir); register_cleanup "rm -rf '$work'"
mkdir -p "$work/home/etc"

"$CC" -o "$work/peer" "$root/tests/lib/dialogue-peer.c" -lssl -lcrypto 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; skip "dialogue-peer does not compile"; }

printf 'good\ttestuser\ts3cret\nbad\ttestuser\tWRONGSECRET\n' > "$work/home/etc/credentials.cfg"
chmod 600 "$work/home/etc/credentials.cfg"

# The challenge is binary-ish on purpose: angle brackets, dots and digits,
# the shape RFC 2195 gives, carried base64 so it must come back decoded.
chal='<1896.697170952@postoffice.example.net>'
cat > "$work/ok.script" <<EOS
send "+OK ready\r\n"
recv AUTH CRAM-MD5
cramchallenge $chal
cramcheck testuser s3cret
send "+OK authenticated\r\n"
hold 5
EOS
cp "$work/ok.script" "$work/bad.script"

: > "$work/pids"
start() {	# script portfile obsfile
	"$work/peer" "$1" "$3" > "$2" &
	echo $! >> "$work/pids"
	local i=0
	while [ "$i" -lt 50 ]; do [ -s "$2" ] && break; sleep 0.1; i=$((i + 1)); done
	cat "$2"
}
pg=$(start "$work/ok.script"  "$work/pg" "$work/og")
pb=$(start "$work/bad.script" "$work/pb" "$work/ob")
register_cleanup "kill $(tr '\n' ' ' < "$work/pids") 2>/dev/null || :"
[ -n "$pg" ] && [ -n "$pb" ] || fail "a peer never named its port"

cramentry() {	# name port credname
cat <<CFG
[$1]
   options banner
   port $2

   state greeting
      credentials $3
      timeout(10)                 -> fail
      expect "+OK"                -> ask

   state ask
      send "AUTH CRAM-MD5\r\n"
      timeout(10)                 -> fail
      expect "+ " as chal         -> answer

   state answer
      chal ~ "\\+ (\\S+)" as b64chal
      send "\${base64:\${username} \${hmac-md5:\${password},\${unbase64:\${b64chal}}}}\r\n"
      timeout(10)                 -> fail
      expect "+OK"                -> success
CFG
}
{ cramentry cramok "$pg" good; echo; cramentry cramwrong "$pb" bad; } \
	> "$work/home/etc/protocols.cfg"
printf '127.0.0.1\tok\t# cramok\n127.0.0.1\twr\t# cramwrong\n' > "$work/home/etc/hosts.cfg"

XYMONHOME="$work/home" "$XYMONNET" --no-update --noping --checkresponse=red \
	--dns=ip --timeout=30 >"$work/out.txt" 2>&1 || :
sleep 1

colour_of() { grep -oE "status\+[0-9]+ $1 (green|yellow|red|clear)" "$work/out.txt" | awk '{print $3}' | head -1; }

grep -q '^cram-ok' "$work/og" || fail \
	"the CRAM-MD5 answer was not what OpenSSL derives for this challenge and
secret. The peer decoded what arrived and recomputed it independently:
$(cat "$work/og")"

[ "$(colour_of ok.cramok)" = green ] || fail \
	"the exchange did not complete:
$(grep -i cramok "$work/out.txt" | head -4)"

# THE CONTROL: the wrong secret must not produce the right digest
grep -q '^cram-ok' "$work/ob" && fail \
	"a login with the WRONG secret was accepted by the peer, so the peer is not
checking the digest and the assertion above proves nothing:
$(cat "$work/ob")"
grep -q '^MISMATCH' "$work/ob" || fail \
	"the wrong-secret peer neither accepted nor rejected: it never received an
answer to judge, so the control did not run:
$(cat "$work/ob")"

pass "CRAM-MD5 works end to end: the challenge is decoded, keyed with HMAC-MD5 and re-encoded, and OpenSSL agrees"
