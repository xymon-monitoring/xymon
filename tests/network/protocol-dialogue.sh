#!/usr/bin/env bash
#
# The dialogue engine must not report a service OK unless every step ran.
#
# This is the assertion the whole feature rests on. Before it, a server
# that greeted "220" and then 421'd every command was reported GREEN --
# xymonnet compared the greeting against the banner prefix and stopped
# looking. Measured against a server that 421s everything after the
# greeting: green before, "Unexpected service response" after.
#
# So the property under test is not "the steps are parsed" but "an
# unfinished dialogue is a failure". A verdict that only checks
# dialogfail would still pass a server that goes silent halfway through,
# because nothing failed -- the conversation just stopped. curstep must
# be NULL too, meaning the list was consumed to the end.
#
# Source assertions rather than a live server: the rest of this suite
# runs without building, and the behaviour above is covered by hand
# against real Postfix. Comments are stripped before matching, because
# this file's own prose contains every identifier it asserts.

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

pass "an unfinished dialogue cannot report OK (both dialogfail and curstep are checked)"
