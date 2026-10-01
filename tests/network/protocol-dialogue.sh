#!/usr/bin/env bash
# The dialogue engine must not report a service OK unless every step ran.
#
# Before it, a server that greeted "220" and then 421d every command was
# GREEN: xymonnet compared the greeting against the banner prefix and
# stopped looking.
#
# Checking dialogfail alone would still pass a server that goes silent
# halfway through -- nothing failed, the conversation just stopped -- so
# curstep must be NULL too, meaning the list was consumed to the end.
#
# Source assertions rather than a live server: the rest of this suite runs
# without building. Comments are stripped before matching, because this
# file's own prose contains every identifier it asserts.

set -eu
. "$(dirname "$0")/../lib/assert.sh"
root=$(find_root)

strip() { sed 's://.*::' "$1" | sed 's:/\*:\n&:g' | sed '/\/\*/,/\*\//d'; }

h=$(strip "$root/lib/netservices.h")
c=$(strip "$root/lib/netservices.c")
n=$(strip "$root/xymonnet/contest.c")

for sym in STEP_SEND STEP_EXPECT svcstep_t TCP_DIALOGUE; do
	printf '%s\n' "$h" | grep -q "$sym" ||
		fail "lib/netservices.h does not define $sym -- the step list has no type"
done

printf '%s\n' "$c" | grep -q 'add_svcstep.*STEP_EXPECT' ||
	fail "the protocols.cfg parser never records an expect step"
printf '%s\n' "$c" | grep -q 'add_svcstep.*STEP_SEND' ||
	fail "the protocols.cfg parser never records a send step"

# THE ASSERTION. Both halves, on the same return.
verdict=$(printf '%s\n' "$n" | grep -n 'dialogfail' | grep 'return' || true)
[ -n "$verdict" ] ||
	fail "no verdict consults dialogfail -- a failed dialogue would still report OK"
printf '%s\n' "$verdict" | grep -q 'curstep' ||
	fail "the verdict checks dialogfail but not curstep: a peer that goes silent
mid-dialogue sets neither failure flag nor finishes the list, so it would be
reported OK -- which is exactly the 421 bug this feature exists to catch"

# The socket must stay open while steps remain, or the dialogue is cut
# off by the very close that the single-shot probe used to do.
printf '%s\n' "$n" | grep -q '!item->curstep' ||
	fail "no close guard mentions curstep -- the connection is torn down mid-dialogue"

# --- the telnet banner is copied before the old one is freed ----------------
#
# do_telnet_options() replaces item->banner with the text following the IAC
# options. Its source pointer, inp, walks item->telnetbuf -- which the read arm
# sets to item->banner itself. So the buffer being replaced IS the buffer being
# read from: freeing first leaves inp dangling and the copy reads freed memory,
# reachable by any telnet peer that sends options and banner text in one read.
#
# A leak fix introduced exactly that, so this pins the order rather than the
# absence of a free. Source assertion because the corruption is silent on a
# plain build -- freed memory usually still holds the bytes -- and nothing here
# runs xymonnet under a sanitizer.
telnet_fn=$(awk '/^static int do_telnet_options/{c=1} c{print} c&&/^}/{exit}' "$root/xymonnet/contest.c")
[ -n "$telnet_fn" ] || fail "contest.c no longer defines do_telnet_options()"

banner_copy=$(grep -n 'strdup' <<<"$telnet_fn" | head -1 | cut -d: -f1)
banner_free=$(grep -n 'xfree(item->banner)' <<<"$telnet_fn" | head -1 | cut -d: -f1)

if [ -n "$banner_free" ]; then
	[ -n "$banner_copy" ] || fail \
		"do_telnet_options() frees the banner without copying it (#450)"
	[ "$banner_copy" -lt "$banner_free" ] || fail \
"do_telnet_options() frees item->banner before copying out of it. inp points
into that buffer -- item->telnetbuf is item->banner -- so the copy reads freed
memory. Copy first, free afterwards:
$(sed -n "$((banner_free - 2)),$((banner_free + 2))p" <<<"$telnet_fn")"
fi

grep -q 'item->telnetbuf = NULL' <<<"$telnet_fn" || fail \
"do_telnet_options() leaves item->telnetbuf pointing at the freed banner. It
aliases the buffer that was just replaced, so it has to be cleared with the
length that goes with it."

pass "an unfinished dialogue cannot report OK, and the telnet banner is copied before the buffer it reads from is freed"
