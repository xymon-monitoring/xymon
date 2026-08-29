#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# A reply framed by a length, not by a line.
#
# LDAP, MySQL, DNS-over-TCP and AMQP put a count in front of a message
# instead of ending it with CRLF or a lone dot. "expect" is prefix-anchored
# on text and "until" names a terminator, so neither can say where such a
# reply stops: before "expect bytes(N)" those services could be probed no
# further than the banner.
#
# The peer here sends a 12-byte frame with NO newline anywhere in it, then
# waits. A test that passes on it has waited for the twelfth byte and for
# nothing else.
#
# THE CONTROL is [short]: the same entry against a peer that sends 6 bytes
# and stops. It must NOT go green -- otherwise "bytes(12)" would be
# satisfied by whatever happened to arrive, which is the bug this exists to
# prevent, and the suite would pass with the length ignored.
#
# The third peer checks that the frame does not eat the stream: it sends the
# 12 bytes and then a line, and the entry reads the frame and then matches
# that line, so what follows a frame must survive it.

set -eu
. "$(dirname "$0")/../lib/assert.sh"
root=$(find_root)

require_bin XYMONNET xymonnet/xymonnet
require_cc

work=$(mktempdir); register_cleanup "rm -rf '$work'"
mkdir -p "$work/home/etc"

"$CC" -o "$work/peer" "$root/tests/lib/dialogue-peer.c" -lssl -lcrypto 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; skip "dialogue-peer does not compile"; }

# 12 bytes, no newline: only a length can say where this ends.
printf 'send ABCDEFGHIJKL\nhold 20\n'                      > "$work/frame.script"
printf 'send ABCDEF\nhold 20\n'                            > "$work/short.script"
printf 'send ABCDEFGHIJKL\nsend +OK done\\r\\n\nhold 20\n' > "$work/tail.script"

: > "$work/pids"
start() {	# script portfile obsfile
	"$work/peer" "$1" "$3" > "$2" &
	echo $! >> "$work/pids"
	local i=0
	while [ "$i" -lt 50 ]; do [ -s "$2" ] && break; sleep 0.1; i=$((i + 1)); done
	cat "$2"
}
pframe=$(start "$work/frame.script" "$work/pf" "$work/of")
pshort=$(start "$work/short.script" "$work/ps" "$work/os")
ptail=$(start  "$work/tail.script"  "$work/pt" "$work/ot")
register_cleanup "kill $(tr '\n' ' ' < "$work/pids") 2>/dev/null || :"
[ -n "$pframe" ] && [ -n "$pshort" ] && [ -n "$ptail" ] || fail "a peer never named its port"

cat > "$work/home/etc/protocols.cfg" <<CFG
[framed]
   options banner
   port $pframe

   state read
      timeout(10)        -> fail
      expect bytes(12)   -> success

[short]
   options banner
   port $pshort

   state read
      timeout(5)         -> fail
      expect bytes(12)   -> success

[framedtail]
   options banner
   port $ptail

   state read
      timeout(10)        -> fail
      expect bytes(12)   -> line

   state line
      timeout(10)        -> fail
      expect "+OK"       -> success
CFG
printf '127.0.0.1\tfr\t# framed\n127.0.0.1\tsh\t# short\n127.0.0.1\ttl\t# framedtail\n' \
	> "$work/home/etc/hosts.cfg"

XYMONHOME="$work/home" "$XYMONNET" --no-update --noping --checkresponse=red \
	--dns=ip --timeout=30 >"$work/out.txt" 2>&1 || :

colour_of() { grep -oE "status\+[0-9]+ $1 (green|yellow|red|clear)" "$work/out.txt" | awk '{print $3}' | head -1; }

[ "$(colour_of fr.framed)" = green ] || fail \
	"a 12-byte frame with no newline in it was not accepted by 'expect bytes(12)',
so a length-framed reply still cannot be read:
$(head -30 "$work/out.txt")"

# THE CONTROL
[ "$(colour_of sh.short)" = green ] && fail \
	"a peer that sent 6 bytes satisfied 'expect bytes(12)'. The length is not
being waited for, so the frame would accept whatever a read happened to
return:
$(grep -i short "$work/out.txt" | head -4)"

[ "$(colour_of tl.framedtail)" = green ] || fail \
	"the line sent after the 12-byte frame was not matched by the next state, so
a frame consumes more than its own bytes:
$(grep -i framedtail "$work/out.txt" | head -4)"

pass "a reply framed by a length is read by 'expect bytes(N)', which waits for exactly that many"
