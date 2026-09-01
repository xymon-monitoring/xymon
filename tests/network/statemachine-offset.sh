#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/network/statemachine-offset.sh
#
# Reading the byte that decides, when it is not the first one.
#
# An expect is anchored at the start of a reply. That is enough for every
# protocol that answers in text, because the part worth checking comes first --
# "250", "+OK", "RFB ". It is not enough for one that answers in binary, where
# the interesting byte sits behind a length that varies with the message, so
# there is no literal that reaches it from the front.
#
# Oracle's listener is the shipped example. Its reply carries a two-byte length,
# a two-byte checksum, and then the byte saying which kind of packet it is. A
# ping is answered with REFUSE -- there is no session to accept, so declining is
# the successful answer -- and an entry that cannot read byte four cannot tell
# that from anything else holding port 1521. It reports a web server there as a
# healthy database listener, which is the failure a monitoring system must not
# have.
#
# "at N" matches the literal at byte N instead of at the start.
#
# THE CONTROL IS THE THIRD ROW. It runs the entry as it is written today -- a
# lone empty expect, which asks only that the server said something -- against
# the SAME refusing peer, and requires it to come out green. Without that row
# the two above would show that "at 4" works, but not that anything was wrong
# without it.
#
# LAYER: the whole path -- parsed from protocols2.cfg, matched by the driver,
# carried out to the colour reported.

set -eu
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

root=$(find_root)
require_bin XYMONNET xymonnet/xymonnet
: "${CC:=cc}"
command -v "$CC" >/dev/null 2>&1 || skip "no C compiler: every row below needs a live peer"

work=$(mktempdir); register_cleanup "rm -rf '$work'"
mkdir -p "$work/home/etc"

"$CC" -o "$work/peer" "$root/tests/lib/dialogue-peer.c" -lssl -lcrypto 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; skip "dialogue-peer does not compile against libssl"; }

: > "$work/pids"
start_peer() {	# script portfile obsfile -> echoes the port
	"$work/peer" "$1" "$3" > "$2" &
	echo $! >> "$work/pids"
	i=0
	while [ "$i" -lt 60 ]; do [ -s "$2" ] && break; sleep 0.1; i=$((i + 1)); done
	cat "$2"
}
register_cleanup "kill \$(tr '\\n' ' ' < '$work/pids') 2>/dev/null || :"

# Two replies of the same shape and length, differing in one byte: the fifth.
# Nothing else can account for a difference in colour.
#
#   00 20     a length, which is why the fifth byte cannot be reached from the
#   00 00     front: on a real listener this varies with the message
#   02 / 04   accepted / refused
#   00 00 00  the rest of the header
accept='\x00\x20\x00\x00\x02\x00\x00\x00'
refuse='\x00\x20\x00\x00\x04\x00\x00\x00'

# recvraw, not recv: the probe sends bytes with no newline in them, and a peer
# waiting for a line would wait for one that never comes.
printf 'recvraw\nsend %s\nhangup\n' "$accept" > "$work/accept.script"
printf 'recvraw\nsend %s\nhangup\n' "$refuse" > "$work/refuse.script"

# The shipped [oratns] is exercised too, through a per-host port, so these rows
# test the entry an installation actually runs rather than a copy of it.
# A ping is answered with REFUSE (type 4): there is no session to accept, so
# declining is the successful answer. "notns" is whatever else may be listening
# on 1521 -- today's entry reports that green.
printf 'recvraw\nsend %s\nhangup\n' "$refuse"              > "$work/tnsok.script"
printf 'recvraw\nsend "HTTP/1.1 200 OK\\r\\n\\r\\n"\nhangup\n' > "$work/notns.script"

pacc=$(start_peer  "$work/accept.script" "$work/p1" "$work/o1")
pref=$(start_peer  "$work/refuse.script" "$work/p2" "$work/o2")
# A peer serves one connection, so the control needs a refusing peer of its own.
pblind=$(start_peer "$work/refuse.script" "$work/p3" "$work/o3")
ptns=$(start_peer   "$work/tnsok.script" "$work/p4" "$work/o4")
pnotns=$(start_peer "$work/notns.script" "$work/p5" "$work/o5")
[ -n "$pacc" ] && [ -n "$pref" ] && [ -n "$pblind" ] || skip "a peer never named its port"
[ -n "$ptns" ] && [ -n "$pnotns" ] || skip "a peer never named its port"

# The entry under test: one state, two alternatives, told apart by a byte that
# is not the first.
entry() {	# name port
	printf '[%s]\n   port %s\n   start ask\n\n' "$1" "$2"
	printf '   state ask\n      send "ping"\n'
	printf '      expect "\\x02" at 4          -> success\n'
	printf '      expect "\\x04" at 4          -> fail\n\n'
}

# The control: the entry as it stands today, which asks only for an answer.
blind() {	# name port
	printf '[%s]\n   port %s\n   start ask\n\n' "$1" "$2"
	printf '   state ask\n      send "ping"\n'
	printf '      expect ""                   -> success\n\n'
}

{ entry ofsok "$pacc"; entry ofsbad "$pref"; blind ofsblind "$pblind"; } \
	> "$work/home/etc/protocols2.cfg"
cp "$root/xymonnet/protocols2.cfg" "$work/home/etc/shipped2.cfg"
sed -n '/^\[oratns\]/,/^\[/p' "$work/home/etc/shipped2.cfg" | sed '$d' >> "$work/home/etc/protocols2.cfg"

{ printf '127.0.0.1\tserving\t# ofsok\n'
  printf '127.0.0.1\trefusing\t# ofsbad\n'
  printf '127.0.0.1\tunchecked\t# ofsblind\n'
  printf '127.0.0.1\tlistener\t# oratns:%s\n' "$ptns"
  printf '127.0.0.1\tnotalistener\t# oratns:%s\n' "$pnotns"
} > "$work/home/etc/hosts.cfg"

XYMONHOME="$work/home" "$XYMONNET" --no-update --noping --checkresponse=red \
	--dns=ip --timeout=20 >"$work/out.txt" 2>&1 || :

colour_of() { grep -oE "status\+[0-9]+ $1 (green|yellow|red|clear)" "$work/out.txt" | awk '{print $3}' | head -1; }

[ "$(colour_of 'serving\.ofsok')" = green ] || fail \
"a listener that ACCEPTED was not reported up (got '$(colour_of 'serving\.ofsok')').
The reply carries 02 at byte four, which 'at 4' is supposed to match:
$(sed 's/^/  peer saw: /' "$work/o1" | head -3)
$(grep -i ofsok "$work/out.txt" | head -3)"

# THE ROW THAT JUSTIFIES THE FEATURE.
[ "$(colour_of 'refusing\.ofsbad')" = red ] || fail \
"a listener that REFUSED the connection was reported
'$(colour_of 'refusing\.ofsbad')' instead of red. Its reply differs from the
healthy one in exactly one byte, the fifth, and reaching it is the whole point
of 'at':
$(sed 's/^/  peer saw: /' "$work/o2" | head -3)
$(grep -i ofsbad "$work/out.txt" | head -3)"

# THE CONTROL: the same refusal, read by an entry that only asks for an answer.
# It MUST come out green -- that green is the bug being fixed, and if it ever
# goes red on its own then the two rows above prove nothing about "at".
[ "$(colour_of 'unchecked\.ofsblind')" = green ] || fail \
"the control did not reproduce the blind check (got
'$(colour_of 'unchecked\.ofsblind')', wanted green). An entry whose only expect
is empty accepts any answer, including a refusal -- if that no longer reports
green, this test is no longer measuring what 'at' adds:
$(grep -i ofsblind "$work/out.txt" | head -3)"

# The probe really spoke first: an entry that matched something the peer sent
# unprompted would pass the rows above without the offset mattering.
grep -q '^gotraw' "$work/o1" || fail \
"the accepting peer never received the probe's send, so the reply it matched
was not an answer to anything:
$(cat "$work/o1")"

# --- the shipped entry, on the two answers that matter ------------------------
tnscol=$(grep -oE 'status\+[0-9]+ listener\.[a-z0-9_]+ ' "$work/out.txt" | awk '{print $2}' | head -1)
[ -n "$tnscol" ] || fail "the shipped [oratns] entry never reported for the listener host:
$(grep -i oratns "$work/out.txt" | head -3)"

tns_colour=$(grep -oE "status\+[0-9]+ listener\.[a-z0-9_]+ (green|yellow|red)" "$work/out.txt" | awk '{print $3}' | head -1)
notns_colour=$(grep -oE "status\+[0-9]+ notalistener\.[a-z0-9_]+ (green|yellow|red)" "$work/out.txt" | awk '{print $3}' | head -1)

[ "$tns_colour" = green ] || fail \
"the shipped [oratns] read a REFUSE -- the answer a healthy listener gives a
ping, because there is no session to accept -- as '$tns_colour' instead of
green:
$(sed 's/^/  peer saw: /' "$work/o4" | head -3)
$(grep -iE 'listener\.' "$work/out.txt" | head -3)"

# The gain over the entry as it stood: something on 1521 that is not a listener.
[ "$notns_colour" = red ] || fail \
"a service on 1521 that answered in HTTP was reported '$notns_colour', not red.
Reading the TNS packet type is what tells a listener from whatever else is
holding the port -- an entry that asks only for an answer calls this healthy:
$(sed 's/^/  peer saw: /' "$work/o5" | head -3)
$(grep -iE 'notalistener\.' "$work/out.txt" | head -3)"

pass "'at N' reads the byte that decides, and the shipped [oratns] tells a listener from a web server on its port"
