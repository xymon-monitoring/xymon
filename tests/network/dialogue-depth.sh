#!/usr/bin/env bash
#
# One definition, several depths, chosen per host.
#
# This is the capability the whole shape is argued on. protocols.cfg is
# one file per installation: a [smtp] definition applies to every host
# carrying the smtp test. So deepening the shipped entries changes what
# every site is alerted on, all at once -- which is why they have stayed
# shallow, and why "the daemon greeted me" is still what smtp means.
#
# "smtp:ok=greeting" asks for THIS host to be checked only as far as that
# state. The definition can then be the full conversation while a site
# that wants banner depth asks for it host by host, and nobody's alerting
# moves without consent.
#
# THE CONTROL IS THE SECOND HOST. The same definition, same peer, no
# depth: it must go RED, because the conversation it describes does not
# complete against this server. Without that, "ok= made it green" would
# be indistinguishable from "the entry passes anyway".
#
# "cred=" is the same idea for credentials: one definition, and which
# account it uses chosen per host. The value is a NAME -- hosts.cfg is
# world-readable for the same reasons protocols.cfg is, so a password
# must no more appear there than in the definition.
#
# A depth is not a different service, so both report in the same column.
# A port is -- smtp:2525 derives smtp_2525 -- but a site running mixed
# depths must not get a column per depth and no comparable history.

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

# Greets correctly and then rejects the command -- the server this whole
# feature exists to catch.
printf 'send 220 ready\r\n\nrecvany\nsend 421 too many connections\r\n\nhold 20\n' > "$work/half.script"

# Records the username it is offered, so the test can see WHICH account
# each host used rather than only that some login happened.
printf 'send 220 ready\r\n\nrecvany\nsend 250 ok\r\n\nhold 20\n' > "$work/login.script"

p1=$(start_peer "$work/half.script"  "$work/o1" "$work/p1")
p2=$(start_peer "$work/half.script"  "$work/o2" "$work/p2")
p3=$(start_peer "$work/login.script" "$work/o3" "$work/p3")
p4=$(start_peer "$work/login.script" "$work/o4" "$work/p4")
register_cleanup "kill $(cat "$work/p1.pid") $(cat "$work/p2.pid") $(cat "$work/p3.pid") $(cat "$work/p4.pid") 2>/dev/null || :"
[ -n "$p1" ] && [ -n "$p2" ] && [ -n "$p3" ] && [ -n "$p4" ] || fail "a peer never named its port"

printf 'accta\tuser-a\tsecret-a\nacctb\tuser-b\tsecret-b\n' > "$work/home/etc/credentials.cfg"

# Two hosts, ONE definition. shallow asks for banner depth; deep does not.
{ printf '127.0.0.1\tshallow\t# depthsvc:%s:ok=greeting\n' "$p1"
  printf '127.0.0.1\tdeep\t# depthsvc:%s\n' "$p2"
  printf '127.0.0.1\thosta\t# loginsvc:%s:cred=accta\n' "$p3"
  printf '127.0.0.1\thostb\t# loginsvc:%s:cred=acctb\n' "$p4"; } > "$work/home/etc/hosts.cfg"
cat > "$work/home/etc/protocols.cfg" <<CFG
[depthsvc]
   options banner
   start greeting

   state greeting
      timeout(5)                  -> fail
      expect "220"                -> ehlo

   state ehlo
      send "ehlo xymonnet\r\n"
      timeout(5)                  -> fail
      expect "250"                -> success

[loginsvc]
   expect "220"
   credentials accta
   send "user \${username}\r\n"
   expect "250"                -> success
   timeout(5)                  -> fail
   options banner
CFG

XYMONHOME="$work/home" "$XYMONNET" --no-update --noping --checkresponse --dns=ip \
	--timeout=20 --debug > "$work/out.txt" 2>&1 || :

grep -q 'Service depthsvc on shallow is OK' "$work/out.txt" || fail \
	"'ok=greeting' did not stop the dialogue at that state, so a site cannot
ask for the shallow check it has today against a definition that is now
the full conversation:
$(grep -i shallow "$work/out.txt" | head -8)"

# THE CONTROL. Same definition, same peer, no depth -- must fail.
grep -q 'Service depthsvc on deep is not OK' "$work/out.txt" || fail \
	"the same definition without a depth still passed against a server that
greets and then rejects everything. Then 'ok=' proves nothing, because
the entry passes either way:
$(grep -i 'on deep' "$work/out.txt" | head -8)"

# A depth is not a different service.
grep -qE 'shallow\.depthsvc ' "$work/out.txt" || fail \
	"the shallow host did not report in the 'depthsvc' column. A depth must
not derive a column of its own, or a site running mixed depths gets one
column per depth and no comparable history:
$(grep -oE '[a-z]+\.depthsvc[a-z_0-9]*' "$work/out.txt" | sort -u | head)"

# cred=: the same definition, a different account per host. The
# definition names 'accta'; hostb asks for 'acctb' and must get it.
grep -q '^got user user-a' "$work/o3" || fail \
	"the host asking for cred=accta did not send that account's username. The
peer saw:
$(cat "$work/o3")"

grep -q '^got user user-b' "$work/o4" || fail \
	"the host asking for cred=acctb sent the definition's account instead of
its own, so credentials cannot be chosen per host. The peer saw:
$(cat "$work/o4")"

pass "one definition serves two depths and two accounts, chosen per host, in one column"
