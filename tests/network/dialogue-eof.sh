#!/usr/bin/env bash
#
# The peer closing is an outcome, not automatically a fault.
#
# After QUIT a server hanging up is the correct end of the conversation,
# and until now it was reported the same as a server that dropped the call
# mid-command. "eof -> TARGET" lets the entry say which it meant.
#
# The control is the pair: the same peer and the same conversation, with
# and without the eof edge. Without it, an eof edge that did nothing would
# still pass, because the hangup is what ends both runs.

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

printf '127.0.0.1\thost\t# eofok eofbad\n' > "$work/home/etc/hosts.cfg"
cat > "$work/home/etc/protocols.cfg" <<CFG
[eofok]
   options banner
   port $p1

   state greeting
      expect "220"                -> farewell

   state farewell
      send "quit\r\n"
      eof                         -> success
      expect "221"                -> success

[eofbad]
   expect "220"
   send "quit\r\n"
   expect "221"                -> success
   options banner
   port $p2

CFG

out=$(XYMONHOME="$work/home" "$XYMONNET" --no-update --noping --checkresponse \
	--dns=ip --timeout=15 --debug 2>&1 || :)
echo "$out" > "$work/out.txt"

grep -q 'Service eofok on host is OK' "$work/out.txt" || fail \
	"the eof edge did not fire, so the conversation that a real server
completes was reported as a failure:
$(grep -i eofok "$work/out.txt" | head -10)"

# THE CONTROL. Same peer, same conversation, no eof edge -- must fail.
# Without this, an eof edge that did nothing would still pass above.
grep -q 'Service eofbad on host is not OK' "$work/out.txt" || fail \
	"an entry with no 'eof' edge still passed when the peer hung up, so the
edge is not what decided the verdict:
$(grep -i eofbad "$work/out.txt" | head -10)"

pass "the peer closing is an outcome when the entry says so, and a fault when it does not"
