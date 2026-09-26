#!/usr/bin/env bash
# What the server said is reported whether or not "options banner" is set.
#
# The option reads as "collect the banner", and that is not what it does: the
# data callback is installed unconditionally and appends every read, so the
# text reaches the status either way. All the option decides is whether the
# socket is read when NO step asks for a read -- an entry with no expect.
#
# Both halves are pinned here because the shipped entries depend on it: 25 of
# them carried the option next to an expect that already forced the read, and
# dropping those lines has to stay a no-op.

set -eu
. "$(dirname "$0")/../lib/assert.sh"
root=$(find_root)

require_bin XYMONNET xymonnet/xymonnet
require_cc

work=$(mktempdir); register_cleanup "rm -rf '$work'"
mkdir -p "$work/home/etc"

"$CC" -o "$work/peer" "$root/tests/lib/dialogue-peer.c" -lssl -lcrypto 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; skip "dialogue-peer does not compile"; }

# A real greeting rather than a marker string: the assertion reads as what an
# operator would look for in the column, and the test doubles as an example of
# what actually lands there.
MARK="220 mx.xymon.local ESMTP Postfix"
printf 'recvany\nsend "%s\\r\\n"\nhold 4\n' "$MARK" > "$work/s"
# A server that greets first, for the entry that sends nothing.
printf 'send "%s\\r\\n"\nhold 4\n' "$MARK" > "$work/s.greet"
printf '127.0.0.1\tbh\t# t1\n' > "$work/home/etc/hosts.cfg"

run() {	# $1 = the entry body, $2 = peer script -> echoes "colour|hasmark"
	"$work/peer" "${2:-$work/s}" "$work/obs" > "$work/port" &
	echo $! >> "$work/pids"
	local i=0
	while [ "$i" -lt 50 ]; do [ -s "$work/port" ] && break; sleep 0.1; i=$((i + 1)); done
	printf '[t1]\n%s\n   port %s\n' "$1" "$(cat "$work/port")" > "$work/home/etc/protocols.cfg"
	XYMONHOME="$work/home" "$XYMONNET" --no-update --noping --checkresponse \
		--dns=ip --timeout=8 >"$work/out" 2>&1 || :
	local colour hasmark
	colour=$(grep -aoE 'bh\.t1 (green|yellow|red|clear)' "$work/out" | awk '{print $2}' | head -1)
	hasmark=no; grep -aq "$MARK" "$work/out" && hasmark=yes
	kill "$(cat "$work/pids" | tail -1)" 2>/dev/null || :
	rm -f "$work/port"
	echo "$colour|$hasmark"
}
register_cleanup "kill \$(cat '$work/pids' 2>/dev/null) 2>/dev/null || :"

# An expect, and no "options banner": the text must still be reported.
got=$(run '   send "probe\r\n"
   expect "220"')
[ "$got" = "green|yes" ] || fail \
	"an entry with an expect and no 'options banner' gave '$got', wanted
'green|yes'. The expect is what forces the read, and the callback that keeps
what came back is installed either way -- so the option next to an expect
decides nothing, and 25 shipped entries were written on that basis:
$(head -12 "$work/out")"

# The same entry WITH the option: identical outcome, which is what makes
# removing the line from those entries a no-op rather than a change.
got=$(run '   send "probe\r\n"
   expect "220"
   options banner')
[ "$got" = "green|yes" ] || fail \
	"adding 'options banner' beside an expect changed the outcome to '$got'.
The two spellings have to agree, or removing the redundant lines from the
shipped entries is a behaviour change:
$(head -12 "$work/out")"

# With NO expect the option is the only thing asking for a read, and that is
# the one case where it still decides something.
got=$(run '   options banner' "$work/s.greet")
[ "$got" = "green|yes" ] || fail \
	"an entry whose only instruction is 'options banner' gave '$got', wanted
'green|yes'. Nothing else asks for a read there, so the option is what makes
the probe listen at all:
$(head -12 "$work/out")"

pass "the server's text is reported with or without 'options banner'; the option only forces a read when no step does"
