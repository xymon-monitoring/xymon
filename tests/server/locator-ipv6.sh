#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/locator-ipv6.sh
#
# The service locator works over IPv6 (#116): xymond_locator listens on an
# IPv6 address given as --listen=[IPv6]:PORT, and the locator client
# (lib/locator.c) reaches one given as [IPv6]:PORT.
#
# Both read the address as IPv4 only: --listen=[::1]:PORT was refused, and the
# client split "[::1]:PORT" at its first ':' and sent nowhere.
#
# Runs the real daemon on [::1] and pings it with lib/xymonlocator, the
# interactive client built beside it, whose start-up ping prints "Locator is
# available". Skips where the host has no ::1, which is asked of its interface
# list first: once ::1 is there, the locator failing to use it is a failure.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin XYMOND_LOCATOR xymond/xymond_locator
require_bin LOCATOR lib/xymonlocator

have_ipv6_loopback || skip "this host has no IPv6 loopback (::1)"

work=$(mktempdir)
export XYMONTMP="$work"

answers() {	# answers ADDR:PORT -- the locator there replies to a ping
	grep -q "Locator is available" <<<"$("$LOCATOR" "$1" </dev/null 2>/dev/null)"
}

# A free UDP port on ::1: the locator exits at once when the port is taken,
# saying why on stderr, before its log is open.
pid= PORT=
for try in 1 2 3 4 5 6 7 8 9 10; do
	p=$(( 20000 + (RANDOM % 20000) ))
	"$XYMOND_LOCATOR" --listen="[::1]:$p" --logfile="$work/locator.log" 2>"$work/start.err" &
	cand=$!
	up=
	for _ in {1..20}; do
		kill -0 "$cand" 2>/dev/null || break
		if answers "[::1]:$p"; then up=1; break; fi
		sleep 0.1
	done
	if [ -n "$up" ]; then pid=$cand; PORT=$p; break; fi
	kill "$cand" 2>/dev/null || true
	wait "$cand" 2>/dev/null || true
	grep -q 'Address already in use' "$work/start.err" || break
done
[ -n "$pid" ] || fail "xymond_locator did not answer on [::1]: $(cat "$work/start.err" "$work/locator.log" 2>/dev/null)"
register_cleanup "stop_process $pid"

assert_contains "Listening on [::1]:$PORT" "$(cat "$work/locator.log")" "the locator did not say it listens on ::1"

pass "xymond_locator listens on [::1], and the locator client reaches it there"
