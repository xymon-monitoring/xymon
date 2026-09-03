#!/usr/bin/env bash
#
# Telnet options arrive before the banner, and in their own reads.
#
# Recorded from telehack.com, which is also where the fixture transcript
# comes from: one IAC command alone, then more IAC commands with the banner
# behind them, then the rest of the greeting. Other servers are worse --
# one BBS sent its banner a byte per read.
#
# So the options cannot be stripped by looking at a single read. This is
# also the only live telnet coverage: [telnet] has no send and no expect,
# so every other test in this suite would stay green if the negotiation
# stopped working entirely.
#
# The server does NOT wait to be answered before greeting: five public
# servers were probed without replying to a single option and all of them
# sent their banner anyway, which is why the probe never writing the
# refusals costs nothing here.

set -eu
. "$(dirname "$0")/../lib/assert.sh"
root=$(find_root)

require_bin XYMONNET xymonnet/xymonnet
require_cc

work=$(mktempdir); register_cleanup "rm -rf '$work'"
mkdir -p "$work/home/etc"

"$CC" -o "$work/peer" "$root/tests/lib/dialogue-peer.c" -lssl -lcrypto 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; skip "dialogue-peer does not compile"; }

BANNER="Connected to TELEHACK port 214"
{
	printf 'send "\\xff\\xfb\\x03"\n'
	printf 'hold 1\n'
	printf 'send "\\xff\\xfb\\x01\\xff\\xfd\\x18\\r\\n%s\\r\\n"\n' "$BANNER"
	printf 'hold 4\n'
} > "$work/t.script"

"$work/peer" "$work/t.script" "$work/t.obs" > "$work/t.port" &
echo $! > "$work/pid"
register_cleanup "kill \$(cat '$work/pid') 2>/dev/null || :"
i=0; while [ "$i" -lt 50 ]; do [ -s "$work/t.port" ] && break; sleep 0.1; i=$((i + 1)); done
port=$(cat "$work/t.port")
[ -n "$port" ] || fail "the peer never named its port"

printf '[telnet]\n   options banner,telnet\n   port %s\n' "$port" > "$work/home/etc/protocols.cfg"
printf '127.0.0.1\tth\t# telnet\n' > "$work/home/etc/hosts.cfg"

XYMONHOME="$work/home" "$XYMONNET" --no-update --noping --dns=ip \
	--timeout=10 >"$work/out" 2>&1 || :

grep -aq 'Service telnet on th is OK' "$work/out" || fail \
	"the telnet check did not report up against a server that greeted it:
$(grep -a 'telnet' "$work/out" | head -3)"

# The whole banner, not the tail of it: the options are stripped from the
# front of a read, so an off-by-one there eats the first character of the
# greeting and the column still looks plausible.
grep -aq "$BANNER" "$work/out" || fail \
	"the banner is missing or truncated. The options share a read with the text
behind them, so what is stripped has to stop exactly where the last IAC
command ends:
$(grep -aE 'onnected|TELEHACK' "$work/out" | head -3)"

# No IAC byte may survive into the reported banner.
if grep -aq "$(printf '\377')" "$work/out"; then
	fail "a raw IAC byte reached the banner: the options were not stripped"
fi

# --- a server that negotiates and then never greets ------------------------
#
# The check used to be "the port opens", so a telnetd that accepted the
# connection and said nothing was green -- wedged, out of PTYs, hung on a
# lookup, all indistinguishable from healthy, with only an empty banner column
# to show for it. The entry now waits for the greeting, so silence is a
# failure. Most servers greet without waiting to be answered -- five public
# ones were checked and all did -- so requiring a greeting costs them nothing.
#
# It is NOT safe for a server that waits: an IBM i host (pub400.com) sends its
# options and then says nothing until they are answered, and the probe never
# answers them. That check times out either way, on every released Xymon and
# on this branch alike, so nothing here made it worse -- but "silence is a
# failure" is only meaningful for the servers that would have spoken.
printf 'send "\\xff\\xfb\\x03"\nhold 8\n' > "$work/mute.script"
"$work/peer" "$work/mute.script" "$work/mute.obs" > "$work/mute.port" &
echo $! >> "$work/pid"
i=0; while [ "$i" -lt 50 ]; do [ -s "$work/mute.port" ] && break; sleep 0.1; i=$((i + 1)); done
muteport=$(cat "$work/mute.port")

if [ -n "$muteport" ]; then
	# The SHIPPED entry, with only its port rewritten -- an inline copy would
	# pass while protocols.cfg still opened the port and asked nothing.
	sed "s/^   port 23\$/   port $muteport/" "$root/xymonnet/protocols.cfg" \
		> "$work/home/etc/protocols.cfg"
	printf '127.0.0.1\tmute\t# telnet\n' > "$work/home/etc/hosts.cfg"

	XYMONHOME="$work/home" "$XYMONNET" --no-update --noping --checkresponse \
		--dns=ip --timeout=10 >"$work/mute.out" 2>&1 || :

	grep -aq 'Service telnet on mute is OK' "$work/mute.out" && fail \
		"a telnet server that negotiated options and then never greeted was
reported UP. Opening the port is not the check -- the greeting behind the
options is, and without it the column is green with nothing in it:
$(grep -a 'mute' "$work/mute.out" | head -3)"
fi

# --- a server that waits to be answered before it greets --------------------
#
# IBM i does this: pub400.com sends IAC DO NEW-ENVIRON and IAC DO
# TERMINAL-TYPE on both 23 and 992, then says nothing at all until the client
# answers them. No released Xymon could check such a host -- the refusals were
# never written, on either code path, so the probe waited for a greeting the
# server was never going to send and reported a connect timeout on a machine
# that was perfectly healthy.
#
# "start iac" answers them: every option is refused (WILL/WONT -> DONT,
# DO/DONT -> WONT), which is what a monitoring probe wants -- it is not going
# to use any of them, and refusing is what gets the server talking.
printf 'send "\\xff\\xfd\\x27\\xff\\xfd\\x18"\nrecvraw\nsend "IBM i greeting\\r\\n"\nhold 5\n' \
	> "$work/wait.script"
"$work/peer" "$work/wait.script" "$work/wait.obs" > "$work/wait.port" &
echo $! >> "$work/pid"
i=0; while [ "$i" -lt 50 ]; do [ -s "$work/wait.port" ] && break; sleep 0.1; i=$((i + 1)); done
waitport=$(cat "$work/wait.port")

if [ -n "$waitport" ]; then
	sed "s/^   port 23\$/   port $waitport/" "$root/xymonnet/protocols.cfg" \
		> "$work/home/etc/protocols.cfg"
	printf '127.0.0.1\twaits\t# telnet\n' > "$work/home/etc/hosts.cfg"

	XYMONHOME="$work/home" "$XYMONNET" --no-update --noping --checkresponse \
		--dns=ip --timeout=12 >"$work/wait.out" 2>&1 || :

	grep -aq 'Service telnet on waits is OK' "$work/wait.out" || fail \
		"a server that waits for its options to be answered was not checked. It
sent IAC DO commands and held the greeting until they were refused; without
the refusals the probe waits out the timeout and calls a healthy host down:
$(grep -a 'waits' "$work/wait.out" | head -3)
peer saw: $(cat "$work/wait.obs" 2>/dev/null)"

	grep -aq 'IBM i greeting' "$work/wait.out" || fail \
		"the greeting behind the negotiation never reached the status:
$(grep -a 'waits' "$work/wait.out" | head -3)"
fi

# --- WONT and DONT are final answers, and must not be answered -------------
#
# RFC 854: WILL is answered DONT and DO is answered WONT, but a WONT or DONT
# is itself the end of that option's negotiation. Replying to one starts a
# ping-pong that never ends -- pub400.com does exactly this, sending WONT
# ECHO and WONT SGA forever to a client that keeps answering DONT, so the
# check burns its whole timeout and reports a healthy host down.
# The second recvraw is what catches a spurious reply: after the WONT there is
# nothing owed, so the only thing that can arrive is a reply that should not
# have been sent. With nothing sent, that read ends at EOF instead.
# The WONT arrives ALONE, as pub400.com sends it -- sharing a read with text
# hides the bug, because the entry finishes on the text before the needless
# reply is ever written. The trailing recvraw catches that reply if it comes;
# with nothing owed it ends at EOF instead.
printf 'send "\\xff\\xfb\\x01"\nrecvraw\nsend "\\xff\\xfc\\x01"\nhold 1\nsend "Ready to serve\\r\\n"\nrecvraw\nhold 3\n' \
	> "$work/loop.script"
"$work/peer" "$work/loop.script" "$work/loop.obs" > "$work/loop.port" &
echo $! >> "$work/pid"
i=0; while [ "$i" -lt 50 ]; do [ -s "$work/loop.port" ] && break; sleep 0.1; i=$((i + 1)); done
loopport=$(cat "$work/loop.port")

if [ -n "$loopport" ]; then
	sed "s/^   port 23\$/   port $loopport/" "$root/xymonnet/protocols.cfg" \
		> "$work/home/etc/protocols.cfg"
	printf '127.0.0.1\tloops\t# telnet\n' > "$work/home/etc/hosts.cfg"

	XYMONHOME="$work/home" "$XYMONNET" --no-update --noping --checkresponse \
		--dns=ip --timeout=10 >"$work/loop.out" 2>&1 || :

	LC_ALL=C grep -aq 'Ready to serve' "$work/loop.out" || fail \
		"the text after a WONT never reached the status. A WONT ends that
option's negotiation and needs no reply; answering it keeps the exchange
going and the greetingaits behind it:
$(grep -a 'loops' "$work/loop.out" | head -3)
peer saw: $(cat "$work/loop.obs" 2>/dev/null)"

	# Exactly one reply: for the WILL, and nothing for the WONT.
	replies=$(LC_ALL=C grep -ac 'gotraw' "$work/loop.obs" 2>/dev/null); : "${replies:=0}"
	[ "$replies" = "1" ] && : || fail \
		"the probe wrote $replies times where one was owed. Only WILL and DO
are answered; replying to a WONT or DONT is what turns a negotiation into a
loop that runs until the timeout:
$(cat "$work/loop.obs" 2>/dev/null)"
fi

# --- the older spelling behaves the same way --------------------------------
#
# "options telnet" and "start iac" are the same negotiation, so they cannot
# answer differently. They used to: the option went through a second
# implementation that stripped the options but never wrote a refusal, and
# answered WONT with DONT when it did -- so an entry written the old way
# looped against a server that "start iac" handles, on the same host, in the
# same file.
printf 'send "\\xff\\xfb\\x01"\nrecvraw\nsend "\\xff\\xfc\\x01"\nhold 1\nsend "Old spelling ready\\r\\n"\nrecvraw\nhold 3\n' \
	> "$work/old.script"
"$work/peer" "$work/old.script" "$work/old.obs" > "$work/old.port" &
echo $! >> "$work/pid"
i=0; while [ "$i" -lt 50 ]; do [ -s "$work/old.port" ] && break; sleep 0.1; i=$((i + 1)); done
oldport=$(cat "$work/old.port")

if [ -n "$oldport" ]; then
	printf '[oldtelnet]\n   options banner,telnet\n   port %s\n' "$oldport" \
		> "$work/home/etc/protocols.cfg"
	printf '127.0.0.1\told\t# oldtelnet\n' > "$work/home/etc/hosts.cfg"

	XYMONHOME="$work/home" "$XYMONNET" --no-update --noping --checkresponse \
		--dns=ip --timeout=10 >"$work/old.out" 2>&1 || :

	oldreplies=$(LC_ALL=C grep -ac 'gotraw' "$work/old.obs" 2>/dev/null); : "${oldreplies:=0}"
	[ "$oldreplies" = "1" ] || fail \
		"'options telnet' wrote $oldreplies times where one was owed. It is the
same negotiation 'start iac' performs and has to answer the same way -- one
refusal for the WILL, and nothing for the WONT:
$(cat "$work/old.obs" 2>/dev/null)"

	LC_ALL=C grep -aq 'Old spelling ready' "$work/old.out" || fail \
		"'options telnet' did not reach the greeting behind the options, which
'start iac' does. The two spellings have to behave the same:
$(grep -a 'old' "$work/old.out" | head -3)
peer saw: $(cat "$work/old.obs" 2>/dev/null)"
fi

pass "telnet options are answered and stripped, the banner survives, silence fails, a waiting server is checked, WONT is not answered, and both spellings agree"
