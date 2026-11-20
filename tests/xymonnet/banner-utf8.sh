#!/usr/bin/env bash
# A service banner is reported with its UTF-8 bytes intact, while genuine
# control bytes are still replaced.
#
# xymonnet replaces "binary data in banner" before putting the greeting in the
# status (xymonnet/xymonnet.c). It tested each byte with isprint(), which is
# false for every byte >= 0x80 in the C locale -- and the signed-char (int)
# cast made the test undefined for them -- so a UTF-8 greeting such as
# "donnees" with accents came out as "donn..es". The replacement now hits only
# C0 control bytes (except whitespace) and DEL, so bytes >= 0x80 survive.
#
# The peer greets with an accented word and an embedded 0x01: the accented
# bytes must reach the status unchanged, and the 0x01 must become '.'.

set -euo pipefail
. "$(dirname "$0")/../lib/assert.sh"
root=$(find_root)

require_bin XYMONNET xymonnet/xymonnet
require_cc

work=$(mktempdir); register_cleanup "rm -rf '$work'"
mkdir -p "$work/home/etc"

# shellcheck disable=SC2046  # flag lists, split on purpose
"$CC" $(xymon_cflags "$root") $(xymon_sslcflags "$root") -o "$work/peer" "$root/tests/lib/dialogue-peer.c" $(xymon_ldflags "$root") -lssl -lcrypto 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; skip "dialogue-peer does not compile"; }

# The accented word as the raw UTF-8 bytes the status must carry (c3 a9 = e').
ACCENT=$(printf 'donn\303\251es')
# The greeting the peer sends: an accented word, and "x\001y" for the control
# byte. dialogue-peer's send understands \xNN, \r and \n.
printf 'recvany\nsend "220 x\\x01y caf\\xc3\\xa9 %s\\r\\n"\nhold 4\n' "$ACCENT" > "$work/s"
printf '127.0.0.1\tbh\t# t1\n' > "$work/home/etc/hosts.cfg"

"$work/peer" "$work/s" "$work/obs" > "$work/port" &
echo $! > "$work/pids"
register_cleanup "stop_process \$(cat '$work/pids' 2>/dev/null)"
i=0
while [ "$i" -lt 50 ]; do [ -s "$work/port" ] && break; sleep 0.1; i=$((i + 1)); done

printf '[t1]\n   send "probe\\r\\n"\n   expect "220"\n   port %s\n' "$(cat "$work/port")" > "$work/home/etc/protocols.cfg"
XYMONHOME="$work/home" "$XYMONNET" --no-update --noping --checkresponse \
	--dns=ip --timeout=8 >"$work/out" 2>&1 || :

grep -aqF "$ACCENT" "$work/out" || fail \
	"the accented banner word did not reach the status intact; its UTF-8 bytes
were replaced as if binary:
$(head -12 "$work/out")"

grep -aqF 'x.y' "$work/out" || fail \
	"the embedded control byte was not replaced with '.':
$(head -12 "$work/out")"

pass "a UTF-8 service banner is reported with its bytes intact, and a control byte is still replaced"
