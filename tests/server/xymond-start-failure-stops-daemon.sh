#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/xymond-start-failure-stops-daemon.sh
#
# When start_xymond() gives up on a xymond that is still running, it stops
# that xymond before failing.
#
# The test calling it exits on the failure, and nothing else knows the
# daemon is there: it outlives the test and keeps its SysV semaphore sets,
# one per channel. NetBSD allows ten sets in all, so after one such failure
# the next xymond in the suite stopped at "Could not get sem: No space left
# on device", and one failing test took two more down with it.
#
# The daemon is made to stay up without ever answering, as in
# xymond-start-deadline.sh: xymond is real, and the client start_xymond probes
# it with is a stub that waits a second and fails. Only the deadline ends the
# wait, with the daemon running -- the case that used to leave it behind.
#
# Needs a built tree: xymond itself.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
# shellcheck source=tests/lib/xymond-daemon.sh
. "$(dirname "$0")/../lib/xymond-daemon.sh"

require_bin XYMOND xymond/xymond
require_shm_segments "$(grep -c 'setup_channel(C_[A-Z_]*, CHAN_MASTER)' "$(find_root)/xymond/xymond.c")"

work=$(mktempdir)
mkdir -p "$work/home/etc" "$work/home/tmp" "$work/home/www"
printf 'page test Test\n127.0.0.1 testhost.example.com # conn\n' > "$work/hosts.cfg"

require_cfg XYMONSERVER_CFG xymond/etcfiles/xymonserver.cfg
sed -e 's|^XYMONHOME=.*|XYMONHOME="'"$work"'/home"|' \
    -e 's|^XYMONTMP=.*|XYMONTMP="'"$work"'/home/tmp"|' \
	"$XYMONSERVER_CFG" > "$work/xymonserver.cfg"

xymond_launch() {
	local port=$1; shift
	"$XYMOND" --no-daemon --listen="127.0.0.1:$port" \
		--hosts="$work/hosts.cfg" --env="$work/xymonserver.cfg" \
		--pidfile="$work/xymond.pid" --checkpoint-file="$work/chk.out" \
		"$@" \
		> "$work/xymond.log" 2>&1 &
	XYMOND_PID=$!
	# start_xymond runs in a subshell below, so its XYMOND_PID does not
	# reach this shell, and xymond removes its own pidfile when it stops.
	printf '%s\n' "$XYMOND_PID" > "$work/launched.pid"
}

printf '#!/bin/sh\nsleep 1\nexit 1\n' > "$work/slow-client"
chmod +x "$work/slow-client"
XYMONCLIENT="$work/slow-client"

# The safety net, so that a failure here does not leave a daemon behind
# either. It runs only when the test exits, after the check below.
register_cleanup "[ -s '$work/launched.pid' ] && kill \$(cat '$work/launched.pid') 2>/dev/null || true"

rc=0
( XYMOND_START_TIMEOUT=3 start_xymond ) > "$work/start.out" 2>&1 || rc=$?
[ "$rc" -ne 0 ] || fail "start_xymond reported success, though no probe ever answered"

[ -s "$work/launched.pid" ] || fail "start_xymond never launched xymond: $(tail -3 "$work/start.out")"
pid=$(cat "$work/launched.pid")
# Non-vacuity: a xymond that never got going would pass the check below
# without anything having been stopped.
grep -q 'Setup complete' "$work/xymond.log" ||
	fail "xymond never finished its setup, so there was no running daemon for start_xymond to stop: $(tail -3 "$work/xymond.log")"

if kill -0 "$pid" 2>/dev/null; then
	fail "xymond (pid $pid) was still running after start_xymond gave up on it -- the test would exit and leave it holding its semaphore sets: $(tail -3 "$work/start.out")"
fi

pass "start_xymond stopped the xymond it gave up on (pid $pid)"
