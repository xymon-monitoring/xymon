#!/usr/bin/env bash
#
# start and transport: where an entry begins, and which probe runs it.
#
# start proves the dialogue does not have to begin at the first line of
# the entry -- which is what makes the file order-free rather than
# positional. The entry below writes 'farewell' ABOVE 'greeting' on
# purpose: read positionally it would send QUIT before the server had
# greeted, which is the #450 bug reintroduced by moving two blocks.
#
# transport proves an unimplemented one is refused rather than silently
# treated as tcp. A datagram entry quietly becoming a stream probe is the
# kind of thing nobody notices until it reports green.
#
# eof has its own file; the edge is used here only to let the healthy
# conversation finish.

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

# greets, takes QUIT, then closes without answering -- as a real server may
printf 'send 220 ready\r\n\nrecvany\nhangup\n' > "$work/bye.script"
# One peer per service: each serves a single connection and then exits,
# so sharing one port between entries makes the run a race.
p1=$(start_peer "$work/bye.script" "$work/o1" "$work/p1")
p2=$(start_peer "$work/bye.script" "$work/o2" "$work/p2")
p3=$(start_peer "$work/bye.script" "$work/o3" "$work/p3")
register_cleanup "kill $(cat "$work/p1.pid") $(cat "$work/p2.pid") $(cat "$work/p3.pid") 2>/dev/null || :"
[ -n "$p1" ] && [ -n "$p2" ] && [ -n "$p3" ] || fail "a peer never named its port"

printf '127.0.0.1\thost\t# eofok eofbad udpsvc\n' > "$work/home/etc/hosts.cfg"
cat > "$work/home/etc/protocols.cfg" <<CFG
[eofok]
   transport tcp
   options banner
   port $p1
   begin greeting


   state farewell
      send "quit\r\n"
      eof                         -> success
      expect "221"                -> success

   state greeting
      expect "220"                -> farewell

[eofbad]
   expect "220"
   send "quit\r\n"
   expect "221"                -> success
   options banner
   port $p2

[udpsvc]
   transport udp
   expect "220"                -> success
   options banner
   port $p3
CFG

out=$(XYMONHOME="$work/home" "$XYMONNET" --no-update --noping --checkresponse \
	--dns=ip --timeout=15 --debug 2>&1 || :)
echo "$out" > "$work/out.txt"

# start: 'farewell' is written above 'greeting', so a positional reading
# would send QUIT before the greeting arrived.
grep -q 'Service eofok on host is OK' "$work/out.txt" || fail \
	"the entry did not begin at its 'start' state, so a positional reading
sent QUIT before the greeting arrived:
$(grep -i eofok "$work/out.txt" | head -10)"

# THE CONTROL. Same peer, same conversation, no eof edge -- must fail.
# Without this, an eof edge that did nothing would still pass above.
grep -q 'Service eofbad on host is not OK' "$work/out.txt" || fail \
	"an entry with no 'eof' edge still passed when the peer hung up, so the
edge is not what decided the verdict:
$(grep -i eofbad "$work/out.txt" | head -10)"

grep -qi "transport 'udp' is not implemented" "$work/out.txt" || fail \
	"an unimplemented transport was accepted. It must refuse, not fall back to
tcp:
$(grep -i udp "$work/out.txt" | head -5)"

grep -q 'Service udpsvc on host is not OK' "$work/out.txt" || fail \
	"the refused transport still reported the service up:
$(grep -i udpsvc "$work/out.txt" | head -5)"

pass "start chooses the entry state, and an unimplemented transport is refused rather than run as tcp"
