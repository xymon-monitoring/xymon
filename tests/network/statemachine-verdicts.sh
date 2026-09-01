#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/network/statemachine-verdicts.sh
#
# What a state machine can say that a straight line cannot.
#
# A positional entry has one way to fail: the reply did not match, and every
# such failure gets the same colour. So a mail server that is briefly out of
# queue space -- "421 try again later", the RFC's own way of saying come back
# -- is reported exactly like one whose relay map is broken and answers 500.
# One is a server asking for a minute; the other needs somebody woken up, and
# an operator who cannot tell them apart learns to ignore the column.
#
# A state names its alternatives and what each of them means, so the file
# decides: 4xx is a warning, 5xx is a failure. THE MIDDLE ROW IS THE POINT --
# it is the one an entry in protocols.cfg cannot express at all.
#
# Every run passes --checkresponse=red, which forces every other kind of
# failure to red. A yellow can therefore only have come from the entry's own
# verdict, not from a default leaking through.
#
# LAYER: the whole path -- protocols2.cfg parsed, edges resolved, the state
# machine driven, and the verdict carried out to the colour that is reported.

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

# Three servers: one healthy, one briefly busy, one broken. They differ only
# in the reply to EHLO, so the reply is the only thing the colour can rest on.
printf '%s\n' 'send "220 mail.test ESMTP\r\n"' 'recv ehlo' \
	      'send "250-mail.test\r\n250 HELP\r\n"' 'hangup'          > "$work/ok.script"
printf '%s\n' 'send "220 mail.test ESMTP\r\n"' 'recv ehlo' \
	      'send "421 4.3.2 try again later\r\n"' 'hangup'          > "$work/busy.script"
printf '%s\n' 'send "220 mail.test ESMTP\r\n"' 'recv ehlo' \
	      'send "500 5.5.1 command unrecognized\r\n"' 'hangup'     > "$work/bad.script"

pok=$(start_peer   "$work/ok.script"   "$work/p1" "$work/o1")
pbusy=$(start_peer "$work/busy.script" "$work/p2" "$work/o2")
pbad=$(start_peer  "$work/bad.script"  "$work/p3" "$work/o3")
[ -n "$pok" ] && [ -n "$pbusy" ] && [ -n "$pbad" ] || skip "a peer never named its port"

entry() {	# name port
	printf '[%s]\n   options banner\n   port %s\n   start greeting\n\n' "$1" "$2"
	printf '   state greeting\n      expect "220" until "220 "    -> ehlo\n\n'
	printf '   state ehlo\n      send "ehlo xymonnet\\r\\n"\n'
	printf '      expect "250" until "250 "    -> success\n'
	printf '      expect "4"                   -> warning\n'
	printf '      expect "5"                   -> fail\n\n'
}
{ entry smok "$pok"; entry smbusy "$pbusy"; entry smbad "$pbad"; } > "$work/home/etc/protocols2.cfg"
printf '127.0.0.1\thealthy\t# smok\n127.0.0.1\tbusy\t# smbusy\n127.0.0.1\tbroken\t# smbad\n' \
	> "$work/home/etc/hosts.cfg"

XYMONHOME="$work/home" "$XYMONNET" --no-update --noping --checkresponse=red \
	--dns=ip --timeout=20 >"$work/out.txt" 2>&1 || :

colour_of() { grep -oE "status\+[0-9]+ $1 (green|yellow|red|clear)" "$work/out.txt" | awk '{print $3}' | head -1; }

[ "$(colour_of 'healthy\.smok')" = green ] || fail \
"a state machine that completed its conversation did not report the service up
(got '$(colour_of 'healthy\.smok')'):
$(grep -i smok "$work/out.txt" | head -3)"

# THE ROW THAT JUSTIFIES THE FEATURE.
[ "$(colour_of 'busy\.smbusy')" = yellow ] || fail \
"a server that answered 421 -- the RFC's \"try again later\" -- was reported
'$(colour_of 'busy\.smbusy')', not yellow. It is the reply a positional entry
cannot tell apart from a broken one, and naming the verdict per alternative is
the whole reason a state exists:
$(sed 's/^/  peer saw: /' "$work/o2" | head -4)"

[ "$(colour_of 'broken\.smbad')" = red ] || fail \
"a server that answered 500 was not reported down (got
'$(colour_of 'broken\.smbad')'):
$(sed 's/^/  peer saw: /' "$work/o3" | head -4)"

# The conversation really ran: an entry that stopped at the greeting would
# still be green above, and prove nothing.
grep -q '^got ehlo' "$work/o1" || fail \
"the healthy peer never received the EHLO, so the machine never left its first
state and the green above means nothing:
$(cat "$work/o1")"

pass "a state machine names what each answer means: 250 up, 421 a warning, 500 down"
