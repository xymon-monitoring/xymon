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

# Strip same-line /* ... */ FIRST. A sed range whose start line also matches
# the end pattern runs on to the NEXT match, so leaving them in deletes real
# code -- which silently emptied two assertions here before this was fixed.
strip() {
	sed -e 's://.*::' -e 's:/\*[^*]*\*/::g' "$1" | sed '/\/\*/,/\*\//d'
}

h=$(strip "$root/lib/netservices.h")
c=$(strip "$root/lib/netservices.c")
n=$(strip "$root/xymonnet/contest.c")

for sym in STEP_SEND STEP_EXPECT STEP_WHEN STEP_JUMP STEP_CREDS \
	   ACT_GOTO ACT_FAIL svcstep_t TCP_DIALOGUE; do
	printf '%s\n' "$h" | grep -q "$sym" ||
		fail "lib/netservices.h does not define $sym -- the step list has no type"
done

for sym in STEP_EXPECT STEP_SEND STEP_WHEN STEP_CAPTURE STEP_CREDS; do
	printf '%s\n' "$c" | grep -q "$sym" ||
		fail "the protocols.cfg parser never records a $sym step"
done

# An unresolved "goto" must not leave a NULL target behind: jumping to it
# would end the dialogue early and report OK.
printf '%s\n' "$c" | grep -q 'st->action = ACT_NEXT' ||
	fail "resolve_svcsteps does not neutralise a goto whose label is missing --
an unresolved branch would jump to NULL and end the dialogue as a success"

# when/else may jump backwards, which is a legal retry loop. Without a cap,
# a loop that performs no I/O spins forever inside the poll loop.
printf '%s\n' "$n" | grep -qE '\+\+guard *>' ||
	fail "dlg_run_instant has no runaway guard -- a backward jump that does no
I/O would hang xymonnet rather than failing the test"

# md5hash() returns a static buffer; freeing it corrupts the allocator.
# Asserted positively -- the negative form passed for free when the grep
# matched nothing at all.
md5line=$(printf '%s\n' "$n" | grep 'md5hash(' | head -1)
[ -n "$md5line" ] ||
	fail "no md5hash() call found -- either the md5 expansion is gone, or the
comment stripper ate it and this assertion is passing for free"
case "$md5line" in
	*freeval*)
		fail "md5hash()'s result is routed through the free-list, but it is a
static buffer -- freeing it corrupts the allocator: $md5line" ;;
esac
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

# starttls upgrades a live connection, so reading must follow the SESSION
# and not the service definition -- keying it off TCP_SSL hands back raw
# TLS records once a plaintext connection has been upgraded.
printf '%s\n' "$n" | grep -q 'STEP_STARTTLS' ||
	fail "no starttls step in the driver"
readfn=$(printf '%s\n' "$n" | awk '/^static int socket_read\(tcptest_t \*item, char \*inbuf/{c=1} c{print} c&&/^}/{exit}')
printf '%s\n' "$readfn" | grep -qE 'if \(item->sslrunning\)' ||
	fail "socket_read() does not dispatch on the live session: an upgraded
connection would keep calling read() and return ciphertext as the reply"

pass "an unfinished dialogue cannot report OK (both dialogfail and curstep are checked)"
