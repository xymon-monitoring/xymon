#!/usr/bin/env bash
#
# A reply that never matches must not be accumulated forever.
#
# The driver appends every read to a per-conversation buffer and keeps it
# until an alternative matches. A server that talks without ever saying
# anything the config waits for would otherwise grow that buffer for as
# long as it cares to send -- and the poll loop runs hundreds of these at
# once, so it is a memory fault on the monitor caused by a remote host.
# MAX_DIALOGUE_BYTES bounds it; this is the test that the bound is real.
#
# It also has to fail by NAME rather than by running out of time. Both end
# the test, so a cap that merely stopped reading would look identical from
# the outside while telling an operator nothing about why -- and would
# still hold the slot for the whole global timeout.
#
# THE CONTROL is [bigreply]: a multi-line reply of about 20 KB that does
# finish. It is the same shape of traffic, under the cap instead of over
# it, and it must go green. Without it a cap set far too low -- or one
# that fired on any large reply -- would pass the assertions below while
# breaking every server with a long capability list.

set -eu
. "$(dirname "$0")/../lib/assert.sh"
root=$(find_root)

require_bin XYMONNET xymonnet/xymonnet
require_cc

work=$(mktempdir); register_cleanup "rm -rf '$work'"
mkdir -p "$work/home/etc"

"$CC" -o "$work/peer" "$root/tests/lib/dialogue-peer.c" -lssl -lcrypto 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; skip "dialogue-peer does not compile"; }

# Each line is ~100 bytes of SMTP continuation, so it accumulates the way a
# real capability list does rather than as one huge write.
line='send "250-XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX\r\n"'

# Over the cap: 500 lines, ~50 KB, and the terminator never arrives.
{ i=0; while [ $i -lt 500 ]; do echo "$line"; i=$((i + 1)); done; echo 'hold 20'; } > "$work/over.script"

# Under it: 200 lines, ~20 KB, properly terminated.
{ i=0; while [ $i -lt 200 ]; do echo "$line"; i=$((i + 1)); done
  printf 'send "250 done\\r\\n"\nrecvany\nhold 5\n'; } > "$work/under.script"

: > "$work/pids"
start() {	# script portfile obsfile
	"$work/peer" "$1" "$3" > "$2" &
	echo $! >> "$work/pids"
	local i=0
	while [ "$i" -lt 50 ]; do [ -s "$2" ] && break; sleep 0.1; i=$((i + 1)); done
	cat "$2"
}
pover=$(start "$work/over.script"  "$work/po" "$work/oo")
punder=$(start "$work/under.script" "$work/pu" "$work/ou")
register_cleanup "kill $(tr '\n' ' ' < "$work/pids") 2>/dev/null || :"
[ -n "$pover" ] && [ -n "$punder" ] || fail "a peer never named its port"

# The per-state budget is deliberately long: if the cap does not fire, this
# test must fail by timing out rather than pass because a timer rescued it.
cat > "$work/home/etc/protocols.cfg" <<CFG
[toobig]
   state one
   timeout(60)                       -> fail
   expect "250" until "250 "         -> success
   port $pover

[bigreply]
   state one
   timeout(60)                       -> fail
   expect "250" until "250 "         -> quit

   state quit
   timeout(10)                       -> fail
   send "quit\r\n"
   eof                               -> success
   port $punder
CFG
printf '127.0.0.1\tover\t# toobig\n127.0.0.1\tunder\t# bigreply\n' > "$work/home/etc/hosts.cfg"

started=$(date +%s)
XYMONHOME="$work/home" "$XYMONNET" --no-update --noping --checkresponse \
	--dns=ip --timeout=90 >"$work/out.txt" 2>&1 || :
elapsed=$(( $(date +%s) - started ))

grep -qi 'exceeded .* bytes with no match' "$work/out.txt" || fail \
	"a server that sent ~50 KB without ever completing the reply was not
stopped by the buffer cap. The buffer grows for as long as a remote host
cares to talk, on a monitor running hundreds of these at once:
$(grep -iE 'toobig|exceed|bytes' "$work/out.txt" | head -5)"

grep -q 'Service toobig on over is not OK' "$work/out.txt" || fail \
	"the cap fired but the service did not report a failure:
$(grep -i toobig "$work/out.txt" | head -5)"

# The cap must end it, not the clock. Both budgets here are far longer than
# this bound, so finishing quickly can only mean the cap decided.
[ "$elapsed" -lt 45 ] || fail \
	"the run took ${elapsed}s, so the test was ended by a timer rather than
by the cap -- which means the cap is not what stopped the reading"

# THE CONTROL: the same traffic, under the cap, must still work.
grep -q 'Service bigreply on under is OK' "$work/out.txt" || fail \
	"a ~20 KB multi-line reply that does terminate was not accepted. The cap
is set too low or fires on any large reply, which would break every server
with a long capability list:
$(grep -i bigreply "$work/out.txt" | head -5)"

pass "an unmatched reply is bounded and fails by name, while a large complete one still passes"
