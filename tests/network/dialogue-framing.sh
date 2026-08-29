#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# A connection whose messages are framed by a length, not by a line.
#
# TCP says nothing about where a message ends; the protocol does, and it does
# it in one of three ways -- a terminator, a count, or the close. The
# greeting protocols use a terminator and every expect consumes a line.
# DNS-over-TCP, MySQL, LDAP and AMQP send a count first, and their payloads
# are binary: a 0x0A inside one is data, not the end of anything.
#
# "framing length(W, big|little)" says so once for the connection. The driver
# then assembles a whole message before any expect looks at it, a literal
# matches the start of the MESSAGE, and a match consumes the message and its
# count together.
#
# Four things have to hold, and each has a control:
#
#   1  a message whose payload CONTAINS newlines is read whole -- the control
#      is that the same bytes under line framing must NOT report the same
#      thing, or the framing attribute is doing nothing
#   2  a count that promises more than the peer sends must not report OK:
#      an incomplete message is unfinished, never "close enough"
#   3  little-endian widths decode the other way round -- the same 5-byte
#      payload written as 05 00 rather than 00 05
#   4  two messages in one write are two messages: the second must still be
#      there for the state after

set -eu
. "$(dirname "$0")/../lib/assert.sh"
root=$(find_root)

require_bin XYMONNET xymonnet/xymonnet
require_cc

work=$(mktempdir); register_cleanup "rm -rf '$work'"
mkdir -p "$work/home/etc"

"$CC" -o "$work/peer" "$root/tests/lib/dialogue-peer.c" -lssl -lcrypto 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; skip "dialogue-peer does not compile"; }

# The peer decodes \xNN and \n itself, so the script must reach it with the
# escapes intact. printf would expand them here -- and "\x00" would end the
# line as a NUL, sending a frame one byte short of what it announced -- so
# every script line is written with %s.
#
# 00 05 | "A\nB\nC" -- five payload bytes, two of them newlines.
printf '%s\n' 'send \x00\x05A\nB\nC' 'hold 20'        > "$work/be.script"
# 05 00 | the same payload, little-endian width
printf '%s\n' 'send \x05\x00A\nB\nC' 'hold 20'        > "$work/le.script"
# 00 09 promised, three delivered
printf '%s\n' 'send \x00\x09ABC' 'hold 20'             > "$work/trunc.script"
# two complete messages in one write
printf '%s\n' 'send \x00\x03ABC\x00\x03DEF' 'hold 20' > "$work/two.script"

: > "$work/pids"
start() {	# script portfile obsfile
	"$work/peer" "$1" "$3" > "$2" &
	echo $! >> "$work/pids"
	local i=0
	while [ "$i" -lt 50 ]; do [ -s "$2" ] && break; sleep 0.1; i=$((i + 1)); done
	cat "$2"
}
# Each peer serves ONE connection, so the control needs a peer of its own
# running the same script rather than a second service on the same port.
pbe=$(start    "$work/be.script"    "$work/p1" "$work/o1")
pbeln=$(start  "$work/be.script"    "$work/p5" "$work/o5")
ple=$(start    "$work/le.script"    "$work/p2" "$work/o2")
ptrunc=$(start "$work/trunc.script" "$work/p3" "$work/o3")
ptwo=$(start   "$work/two.script"   "$work/p4" "$work/o4")
register_cleanup "kill $(tr '\n' ' ' < "$work/pids") 2>/dev/null || :"
[ -n "$pbe" ] && [ -n "$pbeln" ] && [ -n "$ple" ] && [ -n "$ptrunc" ] && [ -n "$ptwo" ] \
	|| fail "a peer never named its port"

cat > "$work/home/etc/protocols.cfg" <<CFG
[framebe]
   options banner
   framing length(2, big)
   port $pbe

   state msg
      timeout(10)     -> fail
      expect "A"      -> success

[frameline]
   options banner
   framing line
   port $pbeln

   state msg
      timeout(5)      -> fail
      expect "A"      -> success

[framele]
   options banner
   framing length(2, little)
   port $ple

   state msg
      timeout(10)     -> fail
      expect "A"      -> success

[frametrunc]
   options banner
   framing length(2, big)
   port $ptrunc

   state msg
      timeout(5)      -> fail
      expect "A"      -> success

[frametwo]
   options banner
   framing length(2, big)
   port $ptwo

   state first
      timeout(10)     -> fail
      expect "A"      -> second

   state second
      timeout(10)     -> fail
      expect "D"      -> success
CFG
{ printf '127.0.0.1\tbe\t# framebe\n127.0.0.1\tln\t# frameline\n'
  printf '127.0.0.1\tle\t# framele\n127.0.0.1\ttr\t# frametrunc\n'
  printf '127.0.0.1\ttw\t# frametwo\n'; } > "$work/home/etc/hosts.cfg"

XYMONHOME="$work/home" "$XYMONNET" --no-update --noping --checkresponse=red \
	--dns=ip --timeout=30 >"$work/out.txt" 2>&1 || :

colour_of() { grep -oE "status\+[0-9]+ $1 (green|yellow|red|clear)" "$work/out.txt" | awk '{print $3}' | head -1; }

# 1 -- a binary message with newlines in it, read whole
[ "$(colour_of be.framebe)" = green ] || fail \
	"a 5-byte message announced by a 2-byte big-endian count was not read:
$(head -20 "$work/out.txt")"

# 1's CONTROL: the same bytes with line framing must behave differently. If
# both spellings pass, 'framing' is being ignored and this suite proves
# nothing.
[ "$(colour_of ln.frameline)" = green ] && fail \
	"the same peer passed under 'framing line' too, so the length prefix is
not being consumed and the framing attribute changes nothing:
$(grep -i frameline "$work/out.txt" | head -4)"

# 3 -- the width read the other way round
[ "$(colour_of le.framele)" = green ] || fail \
	"a little-endian count (05 00) was not decoded as 5:
$(grep -i framele "$work/out.txt" | head -4)"

# 2 -- a message that never completes
[ "$(colour_of tr.frametrunc)" = green ] && fail \
	"a peer that promised 9 bytes and sent 3 reported OK. An incomplete
message is unfinished, and must never be matched as if it had arrived:
$(grep -i frametrunc "$work/out.txt" | head -4)"

# 4 -- two messages in one write stay two messages
[ "$(colour_of tw.frametwo)" = green ] || fail \
	"the second of two messages sent in one write was lost, so a match
consumes more than its own message:
$(grep -i frametwo "$work/out.txt" | head -4)"

# --- what the file may not say -----------------------------------------------
# These four are refused when the file is read, so they never open a socket:
# port 1 is deliberate, and a connection attempt to it would mean the refusal
# did not happen.
cat > "$work/home/etc/protocols.cfg" <<CFG
[badclause]
   framing length(2, big)
   port 1

   state msg
      timeout(5)                 -> fail
      expect "A" until "\r\n"    -> success

[badbytes]
   framing length(2, big)
   port 1

   state msg
      timeout(5)      -> fail
      expect bytes(4) -> success

[badwidth]
   framing length(9, big)
   port 1

   state msg
      timeout(5)      -> fail
      expect "A"      -> success

[badend]
   framing length(2, sideways)
   port 1

   state msg
      timeout(5)      -> fail
      expect "A"      -> success
CFG
printf '127.0.0.1\tbad\t# badclause\n127.0.0.1\tbad2\t# badbytes\n' > "$work/home/etc/hosts.cfg"
printf '127.0.0.1\tbad3\t# badwidth\n127.0.0.1\tbad4\t# badend\n'  >> "$work/home/etc/hosts.cfg"

XYMONHOME="$work/home" "$XYMONNET" --no-update --noping --checkresponse=red \
	--dns=ip --timeout=10 >"$work/bad.txt" 2>&1 || :

grep -qi "until.*framing length" "$work/bad.txt" || fail \
	"'until' under length framing was accepted. The count already says where
the message ends, so the two can only disagree:
$(head -10 "$work/bad.txt")"
grep -qi "bytes(4).*framing length" "$work/bad.txt" || fail \
	"'expect bytes(N)' under length framing was accepted, though the peer's
own count says how long the message is:
$(head -10 "$work/bad.txt")"
grep -qi "width is 1..4" "$work/bad.txt" || fail \
	"a 9-byte length prefix was accepted:
$(head -10 "$work/bad.txt")"
grep -qi "endianness is 'big' or 'little'" "$work/bad.txt" || fail \
	"an unknown endianness was accepted rather than refused:
$(head -10 "$work/bad.txt")"

pass "length framing reads whole binary messages, refuses what it cannot mean, and leaves the next message alone"
