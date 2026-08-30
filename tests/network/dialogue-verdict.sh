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
   options banner
   port $pok

[dlgbad]
   options banner
   port $pbad

   state greeting
      expect "220"                -> capabilities

   state capabilities
      send "ehlo xymonnet\r\n"
      expect "250"                -> farewell

   state farewell
      send "quit\r\n"
      expect "221"                -> success

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

grep -q 'state capabilities' "$work/out.txt" || fail \
	"the failure did not name the state it happened in. A dialogue can fail at
any step, and 'Unexpected service response' alone leaves the reader guessing
which one:
$(grep -i 'dlgbad' "$work/out.txt" | head -2)"

grep -q 'Service legacy on legacy is OK' "$work/out.txt" || fail \
	"a plain send-and-match service stopped working. That path is supposed to
be untouched by the dialogue driver -- a regression here reaches services
this feature does not even use:
$(cat "$work/out.txt")"

grep -q '^got ehlo' "$work/ok.obs" || fail \
	"the healthy peer never received the EHLO; the dialogue did not run:
$(cat "$work/ok.obs")"

pass "a dialogue reports up on success, names the state it failed in, and leaves plain services alone"
