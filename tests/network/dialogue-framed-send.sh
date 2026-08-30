#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# A framed connection is framed in both directions.
#
# "framing length(W, ...)" taught the driver to READ a message that a count
# introduces. Writing one was left to the file, which cannot do it: the
# count depends on how long an expanded ${...} turns out to be, and that is
# not known until the step runs. So a length-framed REQUEST carrying any
# value at all -- a token, a username, a digest -- was unwritable, and the
# framed protocols could be read from and never spoken to.
#
# The count is written here instead, and a terminator is appended, on the
# same reading: framing describes the connection, not one direction of it.
#
# THE CONTROL is [lineframed]: the default framing, where entries have
# always written their own "\r\n". It must go out with NOTHING added. If
# every send were framed, every entry that exists would gain a prefix or a
# trailing byte, and the two mistakes look identical from inside the code.
#
# The peer records what arrived as hex, because a framed message is not a
# line and its count is not printable.
#
# A message too long for its own count is refused on both of the only two
# occasions it can be: when the file is read, if the send is a constant, and
# at the step otherwise -- what an expansion is worth is the one thing about
# a dialogue that reading the file cannot settle. [toowide] is the first,
# [toowideexp] the second.

set -eu
. "$(dirname "$0")/../lib/assert.sh"
root=$(find_root)

require_bin XYMONNET xymonnet/xymonnet
require_cc

work=$(mktempdir); register_cleanup "rm -rf '$work'"
mkdir -p "$work/home/etc"

"$CC" -o "$work/peer" "$root/tests/lib/dialogue-peer.c" -lssl -lcrypto 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; skip "dialogue-peer does not compile"; }

# Each peer greets in its own framing, reads exactly what the probe writes,
# then answers in that framing so the entry can finish.
printf '%s\n' 'send \x00\x05HELLO' 'recvbytes 12' 'send \x00\x04PONG' 'hold 5' > "$work/big.script"
printf '%s\n' 'send \x05\x00HELLO' 'recvbytes 12' 'send \x04\x00PONG' 'hold 5' > "$work/little.script"
printf '%s\n' 'send HELLO\x00'     'recvbytes 5'  'send PONG\x00'     'hold 5' > "$work/term.script"
printf '%s\n' 'send HELLO\r\n'     'recvbytes 6'  'send PONG\r\n'     'hold 5' > "$work/line.script"
printf '%s\n' 'send \x05HELLO'     'hold 5'                                    > "$work/wide.script"
# 200 bytes, which fit a one-byte count -- until ${hex:} doubles them.
printf 'send \\xc8%s\nhold 5\n' "$(printf 'A%.0s' $(seq 200))"                   > "$work/wideexp.script"

: > "$work/pids"
start() {	# script portfile obsfile
	"$work/peer" "$1" "$3" > "$2" &
	echo $! >> "$work/pids"
	local i=0
	while [ "$i" -lt 50 ]; do [ -s "$2" ] && break; sleep 0.1; i=$((i + 1)); done
	cat "$2"
}
pb=$(start "$work/big.script"    "$work/pb" "$work/ob")
pl=$(start "$work/little.script" "$work/pl" "$work/ol")
pt=$(start "$work/term.script"   "$work/pt" "$work/ot")
pn=$(start "$work/line.script"   "$work/pn" "$work/on")
pw=$(start "$work/wide.script"    "$work/pw" "$work/ow")
pe=$(start "$work/wideexp.script" "$work/pe" "$work/oe")
register_cleanup "kill $(tr '\n' ' ' < "$work/pids") 2>/dev/null || :"
[ -n "$pb" ] && [ -n "$pl" ] && [ -n "$pt" ] && [ -n "$pn" ] && [ -n "$pw" ] \
	&& [ -n "$pe" ] || fail "a peer never named its port"

# 300 bytes, which no one-byte count can announce.
toolong=$(printf 'A%.0s' $(seq 300))

cat > "$work/home/etc/protocols.cfg" <<CFG
[framedbig]
   options banner
   framing length(2, big)
   port $pb

   state greeting
      timeout(10)                 -> fail
      expect "HEL" as tok         -> ask

   state ask
      send "PING \${tok}"
      timeout(10)                 -> fail
      expect "PONG"               -> success

[framedlittle]
   options banner
   framing length(2, little)
   port $pl

   state greeting
      timeout(10)                 -> fail
      expect "HEL" as tok         -> ask

   state ask
      send "PING \${tok}"
      timeout(10)                 -> fail
      expect "PONG"               -> success

[framedterm]
   options banner
   framing terminator "\x00"
   port $pt

   state greeting
      timeout(10)                 -> fail
      expect "HEL"                -> ask

   state ask
      send "PING"
      timeout(10)                 -> fail
      expect "PONG"               -> success

[lineframed]
   options banner
   port $pn

   state greeting
      timeout(10)                 -> fail
      expect "HEL"                -> ask

   state ask
      send "PING\r\n"
      timeout(10)                 -> fail
      expect "PONG"               -> success

[toowide]
   options banner
   framing length(1, big)
   port $pw

   state greeting
      timeout(10)                 -> fail
      expect "HEL"                -> ask

   state ask
      send "$toolong"
      timeout(5)                  -> fail
      expect "PONG"               -> success

[toowideexp]
   options banner
   framing length(1, big)
   port $pe

   state greeting
      timeout(10)                 -> fail
      expect "AAA" as tok         -> ask

   state ask
      send "\${hex:\${tok}}"
      timeout(5)                  -> fail
      expect "PONG"               -> success
CFG
printf '127.0.0.1\tbg\t# framedbig\n127.0.0.1\tlt\t# framedlittle\n127.0.0.1\ttm\t# framedterm\n127.0.0.1\tln\t# lineframed\n127.0.0.1\twd\t# toowide\n127.0.0.1\twe\t# toowideexp\n' \
	> "$work/home/etc/hosts.cfg"

XYMONHOME="$work/home" "$XYMONNET" --no-update --noping --checkresponse=red \
	--dns=ip --timeout=30 >"$work/out.txt" 2>&1 || :
sleep 1

colour_of() { grep -oE "status\+[0-9]+ $1 (green|yellow|red|clear)" "$work/out.txt" | awk '{print $3}' | head -1; }
saw() { grep '^gotbytes ' "$1" | head -1 | sed 's/^gotbytes //'; }

# "PING HELLO" is 10 bytes: 000a big-endian, 0a00 little-endian.
[ "$(saw "$work/ob")" = "000a50494e472048454c4c4f" ] || fail \
	"a big-endian count was not written in front of the message. The value the
greeting bound has to be counted by the driver, because the file cannot know
how long it will be:
 got: $(saw "$work/ob")
 want: 000a50494e472048454c4c4f
$(cat "$work/ob")"

[ "$(saw "$work/ol")" = "0a0050494e472048454c4c4f" ] || fail \
	"the little-endian count is not written little-endian. It is the same byte
count as the big-endian case, in the other order -- so this catches a count
that is written but always one way:
 got: $(saw "$work/ol")
 want: 0a0050494e472048454c4c4f"

[ "$(saw "$work/ot")" = "50494e4700" ] || fail \
	"the terminator was not appended to the message. A peer that ends every
message with a sequence is waiting for it:
 got: $(saw "$work/ot")
 want: 50494e4700"

# THE CONTROL
[ "$(saw "$work/on")" = "50494e470d0a" ] || fail \
	"a line-framed send did not go out exactly as the file wrote it. Framing a
send under LINE framing would add a byte to every entry that exists, and its
own \"\\r\\n\" is already there:
 got: $(saw "$work/on")
 want: 50494e470d0a"

for h in bg.framedbig lt.framedlittle tm.framedterm ln.lineframed; do
	[ "$(colour_of $h)" = green ] || fail \
		"$h did not complete:
$(grep -i "${h#*.}" "$work/out.txt" | head -4)"
done

# A count that cannot hold the message is the config's fault, and is said so.
[ "$(colour_of wd.toowide)" = green ] && fail \
	"a 300-byte message was accepted under a one-byte length prefix. The count
written would have wrapped, and the peer would have read 44 bytes and then
resynchronised on the rest as if it were a new message"
grep -qi "'send' does not fit" "$work/out.txt" || fail \
	"a constant send too long for its own length prefix was not refused when
the file was read. Its length is known there, and what is decidable when the
file is read is refused then:
$(grep -i toowide "$work/out.txt" | head -4)"

# An EXPANDING send is not decidable until it runs, so it is caught there.
[ "$(colour_of we.toowideexp)" = green ] && fail \
	"\${hex:\${tok}} doubled a 200-byte value to 400 and was still announced in
one byte. An expansion's length is only known at the step, so that is where
it has to be checked"
grep -qi "message does not fit" "$work/out.txt" || fail \
	"an expanded send too long for its own length prefix failed without saying
why. It cannot be refused when the file is read -- what an expansion is worth
is not knowable there -- so the driver has to say it:
$(grep -i toowideexp "$work/out.txt" | head -4)"

pass "a framed connection is written framed -- count in the declared width and order, terminator appended, and a line-framed send left alone"
