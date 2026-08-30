#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# A value bound by "as NAME" is bytes, not a string.
#
# "expect bytes(N)" and length framing exist for the services whose replies
# are binary -- DNS-over-TCP, LDAP, MySQL, AMQP -- and those put NULs in the
# first few bytes of a message. A bound value stored with strdup() is cut at
# the first one, so ${name} expands to the stump, and a regex tested against
# the name sees only what precedes the NUL. Nothing reports an error: the
# probe compares almost nothing and goes green, or takes an else-arm and goes
# red, for a reason no message names.
#
# Every peer here sends the SAME twelve bytes, "AB\0CDEFGHIJK", and the NUL
# is the third of them. The three green entries each use the bound value in a
# different place, so a value that survives storage but not one of its uses
# still fails here:
#
#   [nulecho]     sends ${reply} back, and the peer counts the bytes it got
#   [nulwhen]     tests the value with a regex that only matches past the NUL
#   [nulcapture]  extracts a group that lies past the NUL, and tests THAT
#
# THE CONTROL is [nulmiss]: the same value, and a pattern that is genuinely
# not in it. It must NOT go green -- otherwise the three above would prove
# only that a "~" edge is always taken, and the suite would pass with the
# value never read at all.

set -eu
. "$(dirname "$0")/../lib/assert.sh"
root=$(find_root)

require_bin XYMONNET xymonnet/xymonnet
require_cc

work=$(mktempdir); register_cleanup "rm -rf '$work'"
mkdir -p "$work/home/etc"

"$CC" -o "$work/peer" "$root/tests/lib/dialogue-peer.c" -lssl -lcrypto 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; skip "dialogue-peer does not compile"; }

# AB\0CDEFGHIJK -- twelve bytes, a NUL third, and no newline anywhere.
payload='\x41\x42\x00\x43\x44\x45\x46\x47\x48\x49\x4a\x4b'
wire='414200434445464748494a4b'

printf 'send %s\nrecvbytes 12\nsend "+OK\\r\\n"\nhold 5\n' "$payload" > "$work/echo.script"
printf 'send %s\nhold 5\n' "$payload"                                 > "$work/when.script"
printf 'send %s\nhold 5\n' "$payload"                                 > "$work/capture.script"
printf 'send %s\nhold 5\n' "$payload"                                 > "$work/miss.script"

: > "$work/pids"
start() {	# script portfile obsfile
	"$work/peer" "$1" "$3" > "$2" &
	echo $! >> "$work/pids"
	local i=0
	while [ "$i" -lt 50 ]; do [ -s "$2" ] && break; sleep 0.1; i=$((i + 1)); done
	cat "$2"
}
pe=$(start "$work/echo.script"    "$work/pe" "$work/oe")
pw=$(start "$work/when.script"    "$work/pw" "$work/ow")
pc=$(start "$work/capture.script" "$work/pc" "$work/oc")
pz=$(start "$work/miss.script"    "$work/pz" "$work/oz")
register_cleanup "kill $(tr '\n' ' ' < "$work/pids") 2>/dev/null || :"
[ -n "$pe" ] && [ -n "$pw" ] && [ -n "$pc" ] && [ -n "$pz" ] || fail "a peer never named its port"

cat > "$work/home/etc/protocols.cfg" <<CFG
[nulecho]
   options banner
   port $pe

   state read
      timeout(10)                 -> fail
      expect bytes(12) as reply   -> echo

   state echo
      send "\${reply}"
      timeout(10)                 -> fail
      expect "+OK"                -> success

[nulwhen]
   options banner
   port $pw

   state read
      timeout(10)                 -> fail
      expect bytes(12) as reply   -> choose

   state choose
      reply ~ "HIJK"              -> success
      else                        -> fail

[nulcapture]
   options banner
   port $pc

   state read
      timeout(10)                 -> fail
      expect bytes(12) as reply   -> choose

   state choose
      reply ~ "(HIJK)" as tail
      tail ~ "HIJK"               -> success
      else                        -> fail

[nulmiss]
   options banner
   port $pz

   state read
      timeout(10)                 -> fail
      expect bytes(12) as reply   -> choose

   state choose
      reply ~ "ZZZZ"              -> success
      else                        -> fail
CFG
printf '127.0.0.1\tec\t# nulecho\n127.0.0.1\twh\t# nulwhen\n127.0.0.1\tcp\t# nulcapture\n127.0.0.1\tms\t# nulmiss\n' \
	> "$work/home/etc/hosts.cfg"

XYMONHOME="$work/home" "$XYMONNET" --no-update --noping --checkresponse=red \
	--dns=ip --timeout=30 >"$work/out.txt" 2>&1 || :
sleep 1

colour_of() { grep -oE "status\+[0-9]+ $1 (green|yellow|red|clear)" "$work/out.txt" | awk '{print $3}' | head -1; }

# What went over the wire, which is the only account of it that a truncating
# ${reply} cannot flatter: the peer counted the bytes and wrote them as hex.
grep -q "^gotbytes $wire\$" "$work/oe" || fail \
	"\${reply} did not send back the twelve bytes that were bound to it. A value
holding a NUL is being stored or expanded as a C string, so everything past
the third byte is gone. The peer saw:
$(cat "$work/oe")"

[ "$(colour_of ec.nulecho)" = green ] || fail \
	"the round trip through \${reply} did not complete:
$(grep -i nulecho "$work/out.txt" | head -4)"

[ "$(colour_of wh.nulwhen)" = green ] || fail \
	"'reply ~ \"HIJK\"' did not match, so the edge was tested against the bytes
before the NUL rather than against the value:
$(grep -i nulwhen "$work/out.txt" | head -4)"

[ "$(colour_of cp.nulcapture)" = green ] || fail \
	"'reply ~ \"(HIJK)\" as tail' extracted nothing: the capture is matching over
strlen(value), which stops at the NUL:
$(grep -i nulcapture "$work/out.txt" | head -4)"

# THE CONTROL
[ "$(colour_of ms.nulmiss)" = green ] && fail \
	"a pattern that is not in the value took its edge anyway. The '~' edges above
are not deciding anything, so this suite would pass on a value never read:
$(grep -i nulmiss "$work/out.txt" | head -4)"

pass "a value bound by 'as NAME' keeps every byte, and is sent, tested and extracted whole"
