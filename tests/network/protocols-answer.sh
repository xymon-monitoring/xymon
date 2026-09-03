#!/usr/bin/env bash
#
# The two entries that cannot match WHAT a server says must still check
# THAT it says something.
#
# oratns and ircd both send a request whose reply is unmatchable: the TNS
# payload sits behind a length-prefixed header, and an IRC numeric is the
# second token on a line starting with the server's own name. Neither can
# be reached by a prefix match, so both entries were written send-only --
# and a send-only entry goes green against a port that accepts the bytes
# and never answers, which is what a wedged listener looks like.
#
# An empty expect closes that: it matches any reply and fails on none.

set -eu
. "$(dirname "$0")/../lib/assert.sh"
root=$(find_root)

require_bin XYMONNET xymonnet/xymonnet
require_cc

work=$(mktempdir); register_cleanup "rm -rf '$work'"
mkdir -p "$work/home/etc"

"$CC" -o "$work/peer" "$root/tests/lib/dialogue-peer.c" -lssl -lcrypto 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; skip "dialogue-peer does not compile"; }

start_peer() {	# $1 = script, $2 = observations, $3 = port file
	"$work/peer" "$1" "$2" > "$3" &
	echo $! >> "$work/pids"
	local i=0
	while [ "$i" -lt 50 ]; do
		[ -s "$3" ] && break
		sleep 0.1
		i=$((i + 1))
	done
	cat "$3"
}

# Answers with bytes that no prefix match could judge -- which is the point:
# the entry is not judging them, only that they arrived.
printf 'recvany\nsend "\\x00\\x20(DESCRIPTION=(ERR=0))"\nhold 5\n' > "$work/talk.script"
# Accepts the request and never answers: a wedged listener.
printf 'recvany\nhold 5\n' > "$work/mute.script"

ptalk=$(start_peer "$work/talk.script" "$work/talk.obs" "$work/talk.port")
pmute=$(start_peer "$work/mute.script" "$work/mute.obs" "$work/mute.port")
register_cleanup "kill \$(cat '$work/pids') 2>/dev/null || :"
[ -n "$ptalk" ] && [ -n "$pmute" ] || fail "a peer never named its port"

# The SHIPPED entries, with only the ports rewritten. Testing a local copy of
# the entry would pass while protocols.cfg itself lost the expect.
sed -e "s/^   port 1521\$/   port $ptalk/" -e "s/^\tport 6667\$/\tport $pmute/" \
	"$root/xymonnet/protocols.cfg" > "$work/home/etc/protocols.cfg"
printf '127.0.0.1\torahost\t# oratns\n127.0.0.1\tircdhost\t# ircd\n' > "$work/home/etc/hosts.cfg"

XYMONHOME="$work/home" "$XYMONNET" --no-update --noping --checkresponse \
	--dns=ip --timeout=10 >"$work/out" 2>&1 || :

grep -q 'Service oratns on orahost is OK' "$work/out" || fail \
	"a listener that answered was not reported up. The reply is deliberately
unmatchable, so the entry must accept ANY bytes -- an empty expect that
rejects a real answer is worse than no expect at all:
$(grep -i orahost "$work/out" | head -3)"

grep -q 'Service ircd on ircdhost is OK' "$work/out" && fail \
	"a server that took the request and never answered was reported UP. That is
what a wedged listener looks like from outside, and a send-only entry cannot
tell it from a healthy one -- which is why these two entries carry an empty
expect:
peer saw: $(cat "$work/ircd.obs" 2>/dev/null || cat "$work/mute.obs" 2>/dev/null)"

pass "oratns and ircd check that the server answered, without matching what it said"
