#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/xymonnet/xymonping.sh
#
# Pins that xymonping pings without root where the system offers an
# unprivileged ICMP socket: run by a user whose group is inside Linux's
# net.ipv4.ping_group_range -- or by any user where the system grants the
# socket without saying so in advance (macOS) -- a copy of the binary, which
# has no suid bit, must report 127.0.0.1 alive; as root it must too. Skips
# for a user the system refuses. The raw-socket fallback is pinned by
# xymonping-unit.sh, which can force it.

set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
# shellcheck source=tests/lib/assert.sh
. "$here/../lib/assert.sh"

require_bin XYMONPING xymonnet/xymonping

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# A copy, not $XYMONPING itself: CI and a real install make that file
# suid-root, and xymonping falls back to a raw socket as root when the
# unprivileged one is refused -- so the original passes with the unprivileged
# path broken. A copy made by anyone but root has no suid bit, so it has no
# such fallback.
cp "$XYMONPING" "$work/xymonping"
if [ "$(id -u)" != 0 ] && [ -u "$work/xymonping" ]; then
	fail "the copy of xymonping kept its suid bit, so this run would not be unprivileged"
fi

# Linux says in advance whether this user may have the unprivileged socket;
# elsewhere (macOS lets any user have it) the run itself tells.
known=0
if [ "$(id -u)" = 0 ]; then
	known=1
elif [ -r /proc/sys/net/ipv4/ping_group_range ]; then
	read -r lo hi < /proc/sys/net/ipv4/ping_group_range
	gid=$(id -g)
	[ "$gid" -ge "$lo" ] && [ "$gid" -le "$hi" ] \
		|| skip "not root, and group $gid is outside net.ipv4.ping_group_range ($lo $hi)"
	known=1
fi

rc=0
"$work/xymonping" 127.0.0.1 >"$work/out" 2>"$work/err" || rc=$?
if [ "$rc" != 0 ] && [ "$known" = 0 ] \
		&& grep -q -E 'Cannot (get|open) [A-Za-z0-9 ]*socket: (Operation not permitted|Permission denied)' "$work/err"; then
	skip "not root, and this system refused an unprivileged ICMP socket"
fi
[ "$rc" = 0 ] || { cat "$work/err" >&2; fail "xymonping 127.0.0.1 exited $rc, where an ICMP socket is available to it"; }
assert_match '^127\.0\.0\.1 is alive \([0-9.]+ (usec|ms)\)$' "$(first_line "$(cat "$work/out")")" \
	"xymonping reports 127.0.0.1"

pass "xymonping pings 127.0.0.1 without being suid-root where an unprivileged ICMP socket is allowed"
