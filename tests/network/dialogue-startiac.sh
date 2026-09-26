#!/usr/bin/env bash
#
# "start iac" negotiates telnet options as a step, for any service.
#
# IAC option negotiation belongs to the telnet protocol, not to port 23:
# mtrek.com:1701 is a MUD and opens with IAC WILL/WONT exchanges, and BBSes
# do the same on whatever port they listen on. As "options telnet" it was
# welded to the services someone listed in advance; as a step, any entry can
# say where in its conversation the negotiation happens.
#
# The assertion that matters is the expect AFTER it: the options share reads
# with the text behind them, so an entry that negotiates must hand the
# following step the greeting and not the IAC bytes in front of it.

set -eu
. "$(dirname "$0")/../lib/assert.sh"
root=$(find_root)

require_bin XYMONNET xymonnet/xymonnet
require_cc

work=$(mktempdir); register_cleanup "rm -rf '$work'"
mkdir -p "$work/home/etc"

"$CC" -o "$work/peer" "$root/tests/lib/dialogue-peer.c" -lssl -lcrypto 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; skip "dialogue-peer does not compile"; }

# The peer writes its log as it goes, but "as it goes" is not "before the
# probe exits": xymonnet closes the socket and returns, and the peer may not
# have been scheduled yet. Poll rather than assert once.
wait_for() {	# $1 = file, $2 = text
	local i=0
	while [ "$i" -lt 40 ]; do
		# LC_ALL=C: the option refusals put invalid UTF-8 on the same line,
		# and grep in a UTF-8 locale can refuse to match a line it cannot
		# decode -- the text is there, and the search says it is not.
		LC_ALL=C grep -aq "$2" "$1" 2>/dev/null && return 0
		sleep 0.1
		i=$((i + 1))
	done
	return 1
}

start_peer() {	# $1 script, $2 obs, $3 portfile
	"$work/peer" "$1" "$2" > "$3" &
	echo $! >> "$work/pids"
	local i=0
	while [ "$i" -lt 50 ]; do [ -s "$3" ] && break; sleep 0.1; i=$((i + 1)); done
	cat "$3"
}
register_cleanup "kill \$(cat '$work/pids' 2>/dev/null) 2>/dev/null || :"

# mtrek's real shape: options first, then more options sharing a read with
# the greeting text.
{
	printf 'send "\\xff\\xfb\\x03"\n'
	printf 'hold 1\n'
	printf 'send "\\xff\\xfb\\x01\\xff\\xfe\\x22Welcome to the game\\r\\n"\n'
	printf 'recvany\n'
	printf 'send "bye\\r\\n"\n'
	printf 'hold 3\n'
} > "$work/m.script"

port=$(start_peer "$work/m.script" "$work/m.obs" "$work/m.port")
[ -n "$port" ] || fail "the peer never named its port"

cat > "$work/home/etc/protocols.cfg" <<CFG
[mud]
   start iac
   expect "Welcome"
   send "quit\r\n"
   options banner
   port $port
CFG
printf '127.0.0.1\tmh\t# mud\n' > "$work/home/etc/hosts.cfg"

XYMONHOME="$work/home" "$XYMONNET" --no-update --noping --checkresponse \
	--dns=ip --timeout=10 >"$work/out" 2>&1 || :

grep -aq 'Service mud on mh is OK' "$work/out" || fail \
	"a service that negotiates options with 'start iac' did not report up. The
step has to consume the IAC commands and leave the expect looking at the text
behind them:
$(grep -a 'mud' "$work/out" | head -4)
peer saw: $(cat "$work/m.obs" 2>/dev/null)"

wait_for "$work/m.obs" quit || fail \
	"the conversation stopped after the negotiation: the peer never received the
send that follows the expect, so the steps did not continue past 'start iac'.
(The option refusals have no line ending, so they share the peer's recorded
line with the command that follows -- match the word, not the start of it):
$(cat "$work/m.obs" 2>/dev/null)"

# --- "options telnet" and "start iac" together ------------------------------
#
# The option is how this was spelled before the step existed, so both may
# appear in the same entry -- a file being migrated one line at a time, or an
# entry that names the option for older Xymons and the step for this one.
# They arm the same negotiation, so together must behave exactly as either
# alone, not negotiate twice or wait for options that already arrived.
port2=$(start_peer "$work/m.script" "$work/m2.obs" "$work/m2.port")
[ -n "$port2" ] || fail "the second peer never named its port"

cat > "$work/home/etc/protocols.cfg" <<CFG
[mudboth]
   start iac
   expect "Welcome"
   send "quit\r\n"
   options banner,telnet
   port $port2
CFG
printf '127.0.0.1\tmh2\t# mudboth\n' > "$work/home/etc/hosts.cfg"

XYMONHOME="$work/home" "$XYMONNET" --no-update --noping --checkresponse \
	--dns=ip --timeout=10 >"$work/out2" 2>&1 || :

grep -aq 'Service mudboth on mh2 is OK' "$work/out2" || fail \
	"'options telnet' and 'start iac' in one entry did not report up. They arm
the same negotiation: together they have to behave as either one alone:
$(grep -a 'mudboth' "$work/out2" | head -4)
peer saw: $(cat "$work/m2.obs" 2>/dev/null)"

wait_for "$work/m2.obs" quit || fail \
	"with both spellings present the conversation stopped after the negotiation:
$(cat "$work/m2.obs" 2>/dev/null)"

pass "'start iac' negotiates options as a step, and coexists with 'options telnet'"
