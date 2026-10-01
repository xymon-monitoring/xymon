#!/usr/bin/env bash
#
# The peer's \xNN must stop after two digits, like getescapestring() does.
#
# lib/encoding.c reads at most two hex digits, so "\x18Connected" in a
# protocols.cfg is byte 0x18 followed by the letter C. A test peer that
# reads hex digits greedily instead makes that "\x18C" -> 0x8C and eats the
# C, so the fixture puts different bytes on the wire than the config it is
# standing in for.
#
# That is worth pinning because of how it fails: the probe behaves
# perfectly, the assertion still fails, and the evidence points at the code
# under test. It cost a wrong bug report against do_telnet_options() before
# the wire was dumped and the peer turned out to be the liar.

set -eu
. "$(dirname "$0")/../lib/assert.sh"
root=$(find_root)

require_bin XYMONNET xymonnet/xymonnet
require_cc

work=$(mktempdir); register_cleanup "rm -rf '$work'"
mkdir -p "$work/home/etc"

"$CC" -o "$work/peer" "$root/tests/lib/dialogue-peer.c" -lssl -lcrypto 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; skip "dialogue-peer does not compile"; }

# \x18 then "Connected": the C is a hex digit, and must NOT be absorbed.
printf 'send "\\x18Connected\\r\\n"\nhold 3\n' > "$work/e.script"
"$work/peer" "$work/e.script" "$work/e.obs" > "$work/e.port" &
echo $! > "$work/pid"
register_cleanup "kill \$(cat '$work/pid') 2>/dev/null || :"
i=0; while [ "$i" -lt 50 ]; do [ -s "$work/e.port" ] && break; sleep 0.1; i=$((i + 1)); done
port=$(cat "$work/e.port")
[ -n "$port" ] || fail "the peer never named its port"

printf '[esc]\n   options banner\n   port %s\n' "$port" > "$work/home/etc/protocols.cfg"
printf '127.0.0.1\teh\t# esc\n' > "$work/home/etc/hosts.cfg"

XYMONHOME="$work/home" "$XYMONNET" --no-update --noping --dns=ip \
	--timeout=8 >"$work/out" 2>&1 || :

grep -q 'Connected' "$work/out" || fail \
	"the peer swallowed the letter after a two-digit hex escape. \"\\x18C\" has
to be 0x18 then 'C' -- reading a third hex digit puts bytes on the wire that
no protocols.cfg could produce, and every assertion downstream then blames
the probe for what the fixture got wrong:
$(grep -a 'onnected' "$work/out" | head -2)"

pass "the peer's \\xNN escape stops after two digits, as getescapestring does"
