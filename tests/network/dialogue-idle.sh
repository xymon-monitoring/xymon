#!/usr/bin/env bash
#
# Slow is not stopped, and one clock cannot tell them apart.
#
# A large EHLO reply trickling in over half a minute is a working server
# on a poor link. Five seconds of silence in the middle of that same
# reply is a server that has died. With only an absolute budget both are
# the same colour, and an operator cannot tell which they are looking at.
#
# idle(N) restarts whenever a byte arrives; timeout(N) does not.
#
# THE CONTROL IS THE SECOND SERVICE. A timer that simply fired would
# satisfy the first assertion on its own -- the peer is silent, something
# expires, the service goes yellow. What has to also hold is that the
# DRIBBLING peer, whose reply takes far longer than the idle budget but
# never pauses for it, is NOT reported idle. That is the whole
# distinction, and it is the half a plain timeout would fail.

set -eu
. "$(dirname "$0")/../lib/assert.sh"
root=$(find_root)

require_bin XYMONNET xymonnet/xymonnet
require_cc

work=$(mktempdir); register_cleanup "rm -rf '$work'"
mkdir -p "$work/home/etc"

cc -o "$work/peer" "$root/tests/lib/dialogue-peer.c" $(pkg-config --cflags --libs openssl 2>/dev/null || echo -lssl -lcrypto) ||
	skip "cannot build the dialogue peer"

start_peer() {
	"$work/peer" "$1" "$2" > "$3" &
	echo $! > "$3.pid"
	i=0
	while [ "$i" -lt 50 ]; do
		[ -s "$3" ] && break
		sleep 0.1
		i=$((i + 1))
	done
	cat "$3"
}

# stops dead after taking the command
printf 'send 220 ready\r\n\nrecvany\nhold 30\n' > "$work/stopped.script"
# answers, but one byte per second. The reply is multi-line and the
# expect below wants its terminator, so the match cannot complete early
# -- the whole 14 bytes have to arrive, taking ~14s against an idle(3).
# NB: the dribble argument keeps its backslashes -- printf would turn
# \r\n into a real CRLF and end the script line there, so the peer would
# dribble five bytes and the reply would never finish.
{ printf 'send 220 ready\r\n\n'
  printf 'recvany\n'
  printf 'dribble 250-a\\r\\n250 b\\r\\n\n'
  printf 'hold 20\n'; } > "$work/slow.script"

pstop=$(start_peer "$work/stopped.script" "$work/o1" "$work/p1")
pslow=$(start_peer "$work/slow.script"    "$work/o2" "$work/p2")
register_cleanup "kill $(cat "$work/p1.pid") $(cat "$work/p2.pid") 2>/dev/null || :"
[ -n "$pstop" ] && [ -n "$pslow" ] || fail "a peer never named its port"

printf '127.0.0.1\thost\t# stopped slow\n' > "$work/home/etc/hosts.cfg"
cat > "$work/home/etc/protocols.cfg" <<CFG
[stopped]
   options banner
   port $pstop
   expect "220"
   send "ehlo xymonnet\r\n"

   state waiting
      idle(3)                     -> warning
      timeout(30)                 -> fail
      expect "250"                -> success

[slow]
   options banner
   port $pslow
   expect "220"
   send "ehlo xymonnet\r\n"

   state waiting
      idle(3)                     -> warning
      timeout(30)                 -> fail
      expect "250" until "250 "   -> success

CFG

start=$(date +%s)
XYMONHOME="$work/home" "$XYMONNET" --no-update --noping --checkresponse --dns=ip \
	--timeout=40 --debug > "$work/out.txt" 2>&1 || :
elapsed=$(( $(date +%s) - start ))

stopcol=$(grep -oE 'host\.stopped (green|yellow|red|clear)' "$work/out.txt" | awk '{print $2}' | head -1)
slowcol=$(grep -oE 'host\.slow (green|yellow|red|clear)' "$work/out.txt" | awk '{print $2}' | head -1)

[ "$stopcol" = "yellow" ] || fail \
	"a server that went silent for longer than idle(3) reported ${stopcol:-nothing},
not yellow. Silence in the middle of a reply is what the idle clock is for:
$(grep -i stopped "$work/out.txt" | head -8)"

# THE CONTROL. Its reply takes ~14s -- more than four times the idle
# budget -- but never pauses for 3s, so the clock must have restarted on
# every byte. A plain timeout would have fired four times over.
[ "$slowcol" = "green" ] || fail \
	"a server answering one byte per second reported ${slowcol:-nothing}, not
green. Its reply takes longer than the idle budget but never pauses for
it, so an idle clock that does not restart on arriving data is being
used as a plain timeout:
$(grep -i 'host\.slow' "$work/out.txt" | head -8)"

# Neither should have reached the 30s absolute budget or the 40s ceiling.
[ "$elapsed" -lt 32 ] || fail \
	"the run took ${elapsed}s; idle(3) is not what ended the silent test."

pass "silence is yellow (${stopcol}) and slowness is not (${slowcol}), in ${elapsed}s"
