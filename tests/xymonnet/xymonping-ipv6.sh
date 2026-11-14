#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/xymonnet/xymonping-ipv6.sh
#
# Pins that xymonping pings an IPv6 address (#727): ::1 answers like any
# host, from stdin and from the command line, beside an IPv4 address in the
# same list, and a result names the address as it was given, so xymonnet
# finds "0:0:0:0:0:0:0:1" under the spelling its hosts.cfg line used.
#
# xymonping takes an unprivileged ICMP socket where the system allows one, and
# a raw one otherwise, which needs root; xymonping.sh pins the unprivileged
# socket on its own. This test runs it when it is root, or when the
# binary is setuid-root -- which xymonping(1) tells the admin to arrange when
# xymonping is the pinger (fping gets the same from its own package), and which
# the CI build does before the suite. Otherwise it skips, rather than acquiring
# the privilege itself: a test must not call sudo, which surprises a developer's
# machine (sudo can mail a security alert for a no-password attempt, #759) and
# which the portability check forbids. It also skips where the host has no route
# to ::1, the only other environment it cannot check; a socket that will not
# open is a failure.

set -euo pipefail
. "$(dirname "$0")/../lib/assert.sh"

require_bin XYMONPING xymonnet/xymonping

if [ "$(id -u)" != 0 ] && [ ! -u "$XYMONPING" ]; then
	skip "xymonping needs root for its raw sockets: run the suite as root, or install xymonping setuid-root"
fi

work=$(mktempdir); register_cleanup "rm -rf '$work'"

ping6() {
	local rc=0
	"$XYMONPING" --retries=0 --timeout=2 "$@" >"$work/out" 2>"$work/err" || rc=$?
	if grep -q -E 'Network is unreachable|Cannot assign requested address|No route to host' "$work/err"; then
		skip "no IPv6 loopback on this host"
	fi
	[ "$rc" = 0 ] || { cat "$work/err" >&2; fail "xymonping $* exited $rc"; }
}

printf '::1\n127.0.0.1\n' | ping6
grep -q -E '^::1 is alive \([0-9.]+ (usec|ms)\)$' "$work/out" \
	|| fail "::1 from stdin is not reported alive: $(cat "$work/out")"
grep -q -E '^127\.0\.0\.1 is alive \([0-9.]+ (usec|ms)\)$' "$work/out" \
	|| fail "127.0.0.1 is not pinged beside an IPv6 address: $(cat "$work/out")"
assert_not_contains 'Dropping' "$(cat "$work/err")" "an address of the list was dropped"

ping6 ::1 </dev/null
assert_match '^::1 is alive' "$(first_line "$(cat "$work/out")")" "::1 on the command line is not pinged"

printf '0:0:0:0:0:0:0:1\n' | ping6
assert_match '^0:0:0:0:0:0:0:1 is alive' "$(first_line "$(cat "$work/out")")" \
	"an IPv6 address is not reported as it was given"

pass "xymonping pings ::1 from stdin and the command line, beside IPv4, and reports it as given"
