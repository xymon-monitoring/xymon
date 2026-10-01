#!/usr/bin/env bash
#
# What a dialogue REPORTS, against three servers.
#
# The point of the feature is the middle row: a server that greets
# correctly and then fails every command was reported green, because the
# probe matched the greeting and stopped looking. That claim is the whole
# justification for the change and nothing else in the suite checks it.
#
# The third row is the one that actually broke during development. A
# service that is still a plain send-and-match must behave exactly as it
# did before -- the dialogue driver is opt-in, and a regression there
# reaches services the feature does not touch. It went unnoticed through
# a green suite, which is why it is pinned here.

set -eu
. "$(dirname "$0")/../lib/assert.sh"
root=$(find_root)

require_bin XYMONNET xymonnet/xymonnet
require_cc

work=$(mktempdir); register_cleanup "rm -rf '$work'"
mkdir -p "$work/home/etc"

"$CC" -o "$work/peer" "$root/tests/lib/dialogue-peer.c" -lssl -lcrypto 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; skip "dialogue-peer does not compile"; }

start_peer() {	# $1 = script file -> echoes the port
	local pf=$1 obs=$2 portf=$3
	"$work/peer" "$pf" "$obs" > "$portf" &
	echo $! > "$portf.pid"
	local i=0
	while [ "$i" -lt 50 ]; do
		[ -s "$portf" ] && break
		sleep 0.1
		i=$((i + 1))
	done
	cat "$portf"
}

# --- healthy: greets, answers EHLO, accepts QUIT -----------------------------
cat > "$work/ok.script" <<'EOS'
send "220 test.local ESMTP\r\n"
recv ehlo
send "250 OK\r\n"
recv quit
send "221 bye\r\n"
EOS

# --- degraded: greets correctly, then refuses everything ---------------------
cat > "$work/bad.script" <<'EOS'
send "220 test.local ESMTP\r\n"
replyall "421 4.7.0 Too many connections\r\n"
EOS

# --- legacy: a plain send-and-match service, unchanged by this feature -------
cat > "$work/legacy.script" <<'EOS'
recvany
send "+OK legacy ready\r\n"
EOS

pok=$(start_peer "$work/ok.script"     "$work/ok.obs"     "$work/ok.port")
pbad=$(start_peer "$work/bad.script"   "$work/bad.obs"    "$work/bad.port")
pleg=$(start_peer "$work/legacy.script" "$work/leg.obs"   "$work/leg.port")
register_cleanup "kill $(cat "$work/ok.port.pid") $(cat "$work/bad.port.pid") $(cat "$work/leg.port.pid") 2>/dev/null || :"
[ -n "$pok" ] && [ -n "$pbad" ] && [ -n "$pleg" ] || fail "a peer never named its port"

cat > "$work/home/etc/protocols.cfg" <<CFG
[dlg]
   expect "220"
   send "ehlo xymonnet\r\n"
   expect "250"
   send "quit\r\n"
   expect "221"
   options banner
   port $pok

[dlgbad]
   expect "220"
   send "ehlo xymonnet\r\n"
   expect "250"
   send "quit\r\n"
   options banner
   port $pbad

[legacy]
   send "probe\r\n"
   expect "+OK"
   options banner
   port $pleg
CFG
printf '127.0.0.1\thealthy\t# dlg\n127.0.0.1\tdegraded\t# dlgbad\n127.0.0.1\tlegacy\t# legacy\n' \
	> "$work/home/etc/hosts.cfg"

XYMONHOME="$work/home" "$XYMONNET" --no-update --noping --checkresponse \
	--dns=ip --timeout=15 >"$work/out.txt" 2>&1 || :
sleep 1

grep -q 'Service dlg on healthy is OK' "$work/out.txt" || fail \
	"the healthy dialogue did not report the service up:
$(cat "$work/out.txt")"

grep -q 'Service dlgbad on degraded is OK' "$work/out.txt" && fail \
	"a server that greets 220 and then answers 421 to every command was
reported UP. Matching the greeting and looking no further is exactly the
coverage gap the dialogue exists to close:
$(sed -n 's/^/  peer saw: /p' "$work/bad.obs" | head -4)"

# Name the step that failed. This used to be written with a "state" directive
# and a grep for its name -- but "state" is not a directive in this parser
# (it belongs to the branching work), so the entry was REFUSED and the grep
# matched the parser's own complaint about it. The assertion passed while the
# verdict it names was never reached. Match what this driver actually
# reports: the failure is at the "250", which is step 3.
grep -qE 'step 3 expected "250"' "$work/out.txt" || fail \
	"the failure did not name the step it happened at. A dialogue can fail at
any of them, and 'Unexpected service response' alone leaves the reader
guessing whether it was the greeting, a reply mid-conversation, or a peer
that went quiet:
$(grep -i 'dlgbad' "$work/out.txt" | head -3)"

grep -q 'Service legacy on legacy is OK' "$work/out.txt" || fail \
	"a plain send-and-match service stopped working. That path is supposed to
be untouched by the dialogue driver -- a regression here reaches services
this feature does not even use:
$(cat "$work/out.txt")"

grep -q '^got ehlo' "$work/ok.obs" || fail \
	"the healthy peer never received the EHLO; the dialogue did not run:
$(cat "$work/ok.obs")"

# The last send had no expect after it, and nothing checked it was sent -- so
# the entry could stop after the 250 and still pass every assertion above.
# Green says the steps ran out, not that they ran.
grep -q '^got quit' "$work/ok.obs" || fail \
	"the healthy peer never received the QUIT. The dialogue reported up while
stopping short of its last step, which is invisible in the colour: an entry
that ends early simply runs out of steps:
$(cat "$work/ok.obs")"

# --- ":s" still means a connect-only check ----------------------------------
#
# A silent test says nothing on the wire, and has always meant nothing is
# read or matched either. Now that most entries START with an expect,
# honouring only the "say nothing" half would leave the probe waiting for a
# banner ":s" says not to ask for.
printf '%s\n' 'hold 6' > "$work/quiet.script"
"$work/peer" "$work/quiet.script" "$work/quiet.obs" > "$work/quiet.port" &
echo $! >> "$work/pids"
i=0; while [ "$i" -lt 60 ]; do [ -s "$work/quiet.port" ] && break; sleep 0.1; i=$((i + 1)); done
pquiet=$(cat "$work/quiet.port")

if [ -n "$pquiet" ]; then
	printf '[dlgquiet]\n   expect "220"\n   send "ehlo xymonnet\\r\\n"\n   expect "250"\n   options banner\n   port %s\n' \
		"$pquiet" > "$work/home/etc/protocols.cfg"
	printf '127.0.0.1\tquiet\t# dlgquiet:s\n' > "$work/home/etc/hosts.cfg"

	XYMONHOME="$work/home" "$XYMONNET" --no-update --noping --checkresponse \
		--dns=ip --timeout=8 >"$work/quiet.out" 2>&1 || :

	grep -q 'Service dlgquiet on quiet is OK' "$work/quiet.out" || fail \
		"a silent test of a dialogue entry did not report the service up. The
port opened, which is the whole of what ':s' asks: it says nothing on the
wire and reads nothing back. Driving the expects anyway makes ':s' wait for a
banner it was told not to ask for, and every quiet service its users monitor
turns red without anything about it changing:
$(grep -i dlgquiet "$work/quiet.out" | head -3)
peer saw: $(cat "$work/quiet.obs" 2>/dev/null)"
fi

# --- a closing code is not a ready greeting ---------------------------------
#
# NNTP greets 200 (posting allowed) or 201 (no posting), which the entry
# matches as "20" because the linear form cannot say "either of these". That
# also matches 205 -- what a server sends when it is CLOSING. Checking the
# reply to QUIT is what separates a server that is ready from one that is on
# its way out.
printf '%s\n' 'send "205 closing connection\r\n"' 'recvany' 'hold 6' > "$work/bye.script"
"$work/peer" "$work/bye.script" "$work/bye.obs" > "$work/bye.port" &
echo $! >> "$work/pids"
i=0; while [ "$i" -lt 50 ]; do [ -s "$work/bye.port" ] && break; sleep 0.1; i=$((i + 1)); done
byeport=$(cat "$work/bye.port")

if [ -n "$byeport" ]; then
	sed "s/^   port 119\$/   port $byeport/" "$root/xymonnet/protocols.cfg" \
		> "$work/home/etc/protocols.cfg"
	printf '127.0.0.1\tbyehost\t# nntp\n' > "$work/home/etc/hosts.cfg"
	byeout=$(XYMONHOME="$work/home" "$XYMONNET" --no-update --noping --checkresponse \
		--dns=ip --timeout=6 2>&1 || :)
	byecolour=$(grep -oE 'byehost\.nntp (green|yellow|red|clear)' <<<"$byeout" | awk '{print $2}' | head -1)

	[ "$byecolour" != "green" ] || fail \
		"a server whose only word was \"205 closing connection\" reported ready.
205 is what NNTP says on the way out, and the greeting pattern has to be loose
enough for both 200 and 201 -- so the reply to QUIT is what tells them apart:
$(grep -i byehost <<<"$byeout" | head -3)"
fi

pass "a dialogue reports up on success, names the step it failed at, leaves plain services alone, and keeps \":s\" a connect-only check"
