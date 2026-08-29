#!/usr/bin/env bash
#
# A dialogue declares its own verdict, and the three verdicts differ.
#
# Today a dialogue that fails gets respcheck_color -- one colour for every
# way of failing. So a server that is BUSY and a server that is BROKEN
# report identically, and the display cannot tell an operator which one
# they are looking at. "expect ... -> warning" and "expect ... -> fail"
# are the config saying which it meant.
#
# The control is the pair, not either half. A single red proves nothing:
# respcheck_color would produce the same colour for both peers, and the
# test would pass while the feature did nothing. What has to hold is that
# the SAME probe run gives the busy peer and the broken peer DIFFERENT
# colours.
#
# "-> success" is the third: reaching it ends the conversation green even
# though the entry has no more steps to run, so the verdict comes from the
# config rather than from running out of file.

set -eu
. "$(dirname "$0")/../lib/assert.sh"
root=$(find_root)

require_bin XYMONNET xymonnet/xymonnet
require_cc

work=$(mktempdir); register_cleanup "rm -rf '$work'"
mkdir -p "$work/home/etc"

cc -o "$work/peer" "$root/tests/lib/dialogue-peer.c" $(pkg-config --cflags --libs openssl 2>/dev/null || echo -lssl -lcrypto) ||
	skip "cannot build the dialogue peer"

start_peer() {	# script obs portfile -> echoes the port
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

# healthy: greets, answers EHLO, answers QUIT
printf 'send 220 ready\r\n\nrecvany\nsend 250 ok\r\n\nrecvany\nsend 221 bye\r\n\nhangup\n' > "$work/ok.script"
# busy: greets 421 and stays up. Not broken -- temporarily unavailable.
printf 'send 421 too many connections\r\n\nhold 20\n' > "$work/busy.script"
# broken: greets correctly, then rejects the command
printf 'send 220 ready\r\n\nrecvany\nsend 500 command unrecognized\r\n\nhold 20\n' > "$work/bad.script"

pok=$(start_peer   "$work/ok.script"   "$work/ok.obs"   "$work/ok.port")
pbusy=$(start_peer "$work/busy.script" "$work/busy.obs" "$work/busy.port")
pbad=$(start_peer  "$work/bad.script"  "$work/bad.obs"  "$work/bad.port")
register_cleanup "kill $(cat "$work/ok.port.pid") $(cat "$work/busy.port.pid") $(cat "$work/bad.port.pid") 2>/dev/null || :"
[ -n "$pok" ] && [ -n "$pbusy" ] && [ -n "$pbad" ] || fail "a peer never named its port"

printf '127.0.0.1\thost\t# termok termbusy termbad\n' > "$work/home/etc/hosts.cfg"
cat > "$work/home/etc/protocols.cfg" <<CFG
[termok]
   expect "220"
   send "ehlo xymonnet\r\n"
   expect "250"
   send "quit\r\n"
   expect "221"                -> success
   options banner
   port $pok

[termbusy]
   options banner
   port $pbusy
   expect "220"                -> proceed
   expect "421"                -> warning

   state proceed
      send "quit\r\n"
      expect "221"                -> success

[termbad]
   options banner
   port $pbad
   expect "220"   -> done
   send "ehlo xymonnet\r\n"
   expect "250"                -> done
   expect "5"                  -> fail

   state done
      send "quit\r\n"
      expect "221"                -> success

CFG

out=$(XYMONHOME="$work/home" "$XYMONNET" --no-update --noping --checkresponse \
	--dns=ip --timeout=15 --debug 2>&1 || :)
echo "$out" > "$work/out.txt"

grep -q 'Service termok on host is OK' "$work/out.txt" || fail \
	"reaching '-> success' did not report the service up. The verdict should
come from the edge, not from running out of steps:
$(grep -i termok "$work/out.txt" | head -10)"

grep -q 'Service termbusy on host is not OK' "$work/out.txt" || fail \
	"the busy peer was reported up:
$(grep -i termbusy "$work/out.txt" | head -10)"

grep -q 'Service termbad on host is not OK' "$work/out.txt" || fail \
	"the broken peer was reported up:
$(grep -i termbad "$work/out.txt" | head -10)"

# THE POINT. Both failed; they must not have failed the same way.
busycol=$(grep -oE 'host\.termbusy (green|yellow|red|clear)' "$work/out.txt" | awk '{print $2}' | head -1)
badcol=$(grep -oE 'host\.termbad (green|yellow|red|clear)' "$work/out.txt" | awk '{print $2}' | head -1)

[ -n "$busycol" ] && [ -n "$badcol" ] || fail \
	"could not read the colours back out of the status messages:
$(grep -E 'host\.(termbusy|termbad)' "$work/out.txt" | head -6)"

[ "$busycol" = "yellow" ] || fail \
	"'-> warning' produced $busycol, not yellow. A busy server is not a broken
one, and that distinction is the whole point of declaring the verdict."

[ "$badcol" = "red" ] || fail \
	"'-> fail' produced $badcol, not red."

[ "$busycol" != "$badcol" ] || fail \
	"the busy peer and the broken peer both reported $busycol, so the terminal
edges are not deciding the colour -- one setting is."

pass "a dialogue declares its verdict: success green, warning yellow ($busycol), fail red ($badcol)"
