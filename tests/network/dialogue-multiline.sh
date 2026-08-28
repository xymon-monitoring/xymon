#!/usr/bin/env bash
#
# A multi-line reply, which is what real SMTP sends.
#
# An expect consumes one line. EHLO answers with several, so without a way
# to say where the reply ENDS the remaining lines stay in the buffer and
# the next expect compares against "250-PIPELINING" and fails. Both
# services here are identical apart from the "until" clause, so the test
# carries its own control: if the second one starts passing, the
# terminator has stopped doing anything and this test is no longer
# checking what it claims to.
#
# Both end with an expect on the 221, and that matters: with nothing after
# the multi-line reply the leftover lines have nothing to collide with and
# even the broken entry passes. That is exactly why the shipped [smtp]
# definition gets away without a terminator today.

set -eu
. "$(dirname "$0")/../lib/assert.sh"
root=$(find_root)

require_bin XYMONNET xymonnet/xymonnet
require_cc

work=$(mktempdir); register_cleanup "rm -rf '$work'"
mkdir -p "$work/home/etc"

"$CC" -o "$work/peer" "$root/tests/lib/dialogue-peer.c" -lssl -lcrypto 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; skip "dialogue-peer does not compile"; }

cat > "$work/ml.script" <<'EOS'
send "220 test.local ESMTP\r\n"
recv ehlo
send "250-test.local\r\n250-PIPELINING\r\n250 CHUNKING\r\n"
recv quit
send "221 bye\r\n"
EOS

start() {
	"$work/peer" "$work/ml.script" "$2" > "$1" &
	echo $! >> "$work/pids"
	local i=0
	while [ "$i" -lt 50 ]; do [ -s "$1" ] && break; sleep 0.1; i=$((i + 1)); done
	cat "$1"
}
: > "$work/pids"
p1=$(start "$work/p1" "$work/o1")
p2=$(start "$work/p2" "$work/o2")
register_cleanup "kill $(tr '\n' ' ' < "$work/pids") 2>/dev/null || :"
[ -n "$p1" ] && [ -n "$p2" ] || fail "a peer never named its port"

cat > "$work/home/etc/protocols.cfg" <<CFG
[mlok]
   expect "220"
   send "ehlo xymonnet\r\n"
   expect "250" until "250 "
   send "quit\r\n"
   expect "221"
   options banner
   port $p1

[mlbad]
   expect "220"
   send "ehlo xymonnet\r\n"
   expect "250"
   send "quit\r\n"
   expect "221"
   options banner
   port $p2
CFG
printf '127.0.0.1\twith\t# mlok\n127.0.0.1\twithout\t# mlbad\n' > "$work/home/etc/hosts.cfg"

XYMONHOME="$work/home" "$XYMONNET" --no-update --noping --checkresponse \
	--dns=ip --timeout=15 >"$work/out.txt" 2>&1 || :
sleep 1

grep -q 'Service mlok on with is OK' "$work/out.txt" || fail \
	'expect ... until "250 " did not consume the multi-line reply:
'"$(cat "$work/out.txt")"

grep -q 'Service mlbad on without is OK' "$work/out.txt" && fail \
	"the entry WITHOUT a terminator passed too, so the terminator is not what
made the difference and this test proves nothing. Either an expect has
started consuming whole replies by itself, or the peer stopped sending
more than one line."

grep -q 'got quit' "$work/o1" || fail \
	"the dialogue never reached QUIT: $(cat "$work/o1")"

pass "a multi-line reply is consumed through its terminator, and is not without one"
