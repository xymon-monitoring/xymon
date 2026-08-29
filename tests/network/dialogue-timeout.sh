#!/usr/bin/env bash
#
# A step that runs out of time must say WHICH step, must not spend the
# whole test budget doing it, and must stay silent when the service is fine.
#
# Without a per-step budget the only clock is --timeout, which ends the
# entire test and reports a bare connect timeout. A conversation that was
# nine tenths finished is then indistinguishable from a host that never
# answered, and the operator has nothing to act on.
#
# Three things are checked, and the second is the one that stops this test
# being satisfied by a timer that simply fires all the time:
#
#   [budgeted]  a peer that greets, takes the command, then holds the
#               connection open saying nothing -- the budget must end it
#   [healthy]   the same budget against a peer that answers promptly -- the
#               budget must NOT fire, so a working service is unaffected
#   [ceiling]   a budget far larger than --timeout -- the global cutoff must
#               still win, since per-step budgets may never sum past it
#
# The peers hold rather than hang up: EOF is a different code path and a
# different verdict, and would pass this test for the wrong reason.

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

# Greets, accepts one line, then goes quiet while staying connected.
printf 'send 220 ready\r\n\nrecvany\nhold 30\n' > "$work/silent.script"
# Greets, accepts one line, answers it, then quits cleanly.
printf 'send 220 ready\r\n\nrecvany\nsend 250 ok\r\n\nhold 30\n' > "$work/quick.script"

psilent=$(start_peer "$work/silent.script" "$work/silent.obs" "$work/silent.port")
pquick=$(start_peer  "$work/quick.script"  "$work/quick.obs"  "$work/quick.port")
pceil=$(start_peer   "$work/silent.script" "$work/ceil.obs"   "$work/ceil.port")
register_cleanup "kill $(cat "$work/silent.port.pid") $(cat "$work/quick.port.pid") $(cat "$work/ceil.port.pid") 2>/dev/null || :"
[ -n "$psilent" ] && [ -n "$pquick" ] && [ -n "$pceil" ] || fail "a peer never named its port"

printf '127.0.0.1\tsilent\t# budgeted healthy\n' > "$work/home/etc/hosts.cfg"
cat > "$work/home/etc/protocols.cfg" <<CFG
[budgeted]
   expect "220"
   send "ehlo xymonnet\r\n"
   state waiting
   timeout 2
   expect "250"
   options banner
   port $psilent

[healthy]
   expect "220"
   send "ehlo xymonnet\r\n"
   state waiting
   timeout 2
   expect "250"
   options banner
   port $pquick
CFG

start=$(date +%s)
XYMONHOME="$work/home" "$XYMONNET" --no-update --noping --checkresponse --dns=ip \
	--timeout=25 --debug > "$work/out.txt" 2>&1 || :
elapsed=$(( $(date +%s) - start ))

grep -qi 'timed out after 2s' "$work/out.txt" || fail \
	"the step budget did not report itself. Expected a message naming the
state and the budget; got:
$(grep -iE 'budgeted|timed' "$work/out.txt" | head -20)"

grep -qi 'state waiting' "$work/out.txt" || fail \
	"the timeout did not name the state it expired in, so the report cannot
say which part of the conversation stalled:
$(grep -i 'timed out' "$work/out.txt" | head -5)"

# THE CONTROL. A timer that fires regardless would satisfy everything above.
grep -q 'Service healthy on silent is OK' "$work/out.txt" || fail \
	"a service that answered within its budget was still reported down, so the
budget is firing on healthy conversations:
$(grep -i 'healthy' "$work/out.txt" | head -10)"

# 2s budget under a 25s ceiling: near 25 means the budget was ignored.
[ "$elapsed" -lt 15 ] || fail \
	"the run took ${elapsed}s with a 2 second step budget and a 25 second
ceiling, so the budget is not what ended it -- the global cutoff is."

# --- the global ceiling still wins -----------------------------------------
#
# A budget larger than --timeout must not extend the test. Asserted
# separately because it needs a different ceiling on the command line.

printf '127.0.0.1\tsilent\t# ceiling\n' > "$work/home/etc/hosts.cfg"
cat > "$work/home/etc/protocols.cfg" <<CFG
[ceiling]
   expect "220"
   send "ehlo xymonnet\r\n"
   state waiting
   timeout 60
   expect "250"
   options banner
   port $pceil
CFG

start=$(date +%s)
XYMONHOME="$work/home" "$XYMONNET" --no-update --noping --checkresponse --dns=ip \
	--timeout=4 --debug > "$work/ceil.txt" 2>&1 || :
ceilelapsed=$(( $(date +%s) - start ))

[ "$ceilelapsed" -lt 20 ] || fail \
	"a 60 second step budget outlived the 4 second --timeout: the run took
${ceilelapsed}s. Per-state budgets may never sum past the global ceiling."

grep -q 'Service ceiling on silent is not OK' "$work/ceil.txt" || fail \
	"the service was not reported down when the global cutoff ended it:
$(grep -i 'ceiling' "$work/ceil.txt" | head -10)"

pass "a step budget ends its own state (${elapsed}s), leaves a healthy service alone, and never outlives --timeout (${ceilelapsed}s)"
