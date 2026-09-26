#!/usr/bin/env bash
#
# An entry with no steps still gets its banner.
#
# "options banner" with nothing to say is a legal entry: connect, read what
# the server volunteers, report it. It has no send and no expect, so the
# driver has no step to wait on -- and the readpending flag it sets from the
# current step would then park the socket outside readfds and close it
# unread. The banner column comes back empty, and nothing else goes wrong:
# the service stays green, so the loss is invisible until someone looks for
# the text that is no longer there.
#
# Shipped entries of this shape are all telnet, which negotiates before it
# greets and stays on the older path. This is for the hand-written ones.

set -eu
. "$(dirname "$0")/../lib/assert.sh"
root=$(find_root)

require_bin XYMONNET xymonnet/xymonnet
require_cc

work=$(mktempdir); register_cleanup "rm -rf '$work'"
mkdir -p "$work/home/etc"

"$CC" -o "$work/peer" "$root/tests/lib/dialogue-peer.c" -lssl -lcrypto 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; skip "dialogue-peer does not compile"; }

BANNER="Welcome to the step-less banner"
printf 'send "%s\\r\\n"\nhold 4\n' "$BANNER" > "$work/b.script"
"$work/peer" "$work/b.script" "$work/b.obs" > "$work/b.port" &
echo $! > "$work/pid"
register_cleanup "kill \$(cat '$work/pid') 2>/dev/null || :"
i=0; while [ "$i" -lt 50 ]; do [ -s "$work/b.port" ] && break; sleep 0.1; i=$((i + 1)); done
port=$(cat "$work/b.port")
[ -n "$port" ] || fail "the peer never named its port"

printf '[banonly]\n   options banner\n   port %s\n' "$port" > "$work/home/etc/protocols.cfg"
printf '127.0.0.1\tbh\t# banonly\n' > "$work/home/etc/hosts.cfg"

XYMONHOME="$work/home" "$XYMONNET" --no-update --noping --dns=ip \
	--timeout=8 >"$work/out" 2>&1 || :

grep -q "$BANNER" "$work/out" || fail \
	"an entry with no steps did not report its banner. The socket has to be
read even with no step waiting on it -- otherwise 'options banner' collects
nothing, and the check stays green while the column it exists to fill is
empty:
$(head -20 "$work/out")"

pass "an entry with no steps still collects its banner"
