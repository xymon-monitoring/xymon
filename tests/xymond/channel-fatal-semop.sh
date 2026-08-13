#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/xymond/channel-fatal-semop.sh
#
# xymond_channel waits for work by blocking on the GOCLIENT semaphore. If the
# channel's semaphore set goes away under it -- xymond torn down, a stale set
# cleared by hand, ipcrm -- that semop stops blocking and starts failing
# immediately and permanently with EIDRM. The "Semaphore wait aborted" branch
# only continue'd, and the continue skips the rest of the loop body including
# its one blocking call, so an idle waiter turned into a full-core busy-loop
# that ran until someone killed it. It must stop instead.
#
# Driven against the real thing: the harness creates the channel's IPC the way
# xymond does (CHAN_MASTER), and removes the semaphore set once xymond_channel
# has attached to it.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
# shellcheck source=tests/lib/build-worker.sh
. "$(dirname "$0")/../lib/build-worker.sh"

require_bin XYMOND_CHANNEL xymond/xymond_channel

work=$(mktempdir)
# setup_channel() keys the IPC off ftok(XYMONHOME), so a per-test directory
# keeps this run's channel distinct from a real Xymon on the same host and
# from another copy of this test running beside it.
mkdir -p "$work/home"

build_xymond_worker "$work" channel-harness tests/xymond/channel-fatal-semop-harness.c

ids=$(XYMONHOME="$work/home" "$work/channel-harness" create status) \
	|| fail "the harness could not create the status channel"
eval "$ids"
if [ -z "${SEMID:-}" ] || [ -z "${SHMID:-}" ]; then
	fail "the harness did not report the channel ids"
fi
register_cleanup "'$work/channel-harness' rmall '$SHMID' '$SEMID' >/dev/null 2>&1 || true"

XYMONHOME="$work/home" "$XYMOND_CHANNEL" --channel=status /bin/cat \
	>"$work/channel.log" 2>&1 &
worker=$!
register_cleanup "kill -9 $worker 2>/dev/null || true"

# Breaking the channel before xymond_channel has attached would make it fail
# during startup instead, and the test would pass without ever reaching the
# loop under test. setup_channel() registers a client by raising CLIENTCOUNT,
# so wait for that -- the condition, not a duration.
attached=no
for _ in $(seq 1 100); do
	[ "$("$work/channel-harness" clients "$SEMID" 2>/dev/null || echo 0)" -ge 1 ] \
		&& { attached=yes; break; }
	sleep 0.1
done
[ "$attached" = yes ] || {
	cat "$work/channel.log" >&2
	fail "xymond_channel never attached to the channel"
}

# It is now blocked in semop(GOCLIENT). Take the semaphore set away: that wait
# fails with EIDRM, and so will every retry.
"$work/channel-harness" rmsem "$SEMID" >/dev/null \
	|| fail "the harness could not remove the semaphore set"

stopped=no
for _ in $(seq 1 100); do
	kill -0 "$worker" 2>/dev/null || { stopped=yes; break; }
	sleep 0.1
done

if [ "$stopped" != yes ]; then
	# Report what it is doing with the CPU where that is readable: the burn
	# is the symptom operators actually see. Linux-only, so best-effort.
	ticks=$(awk '{print $14+$15}' "/proc/$worker/stat" 2>/dev/null || echo "unavailable")
	cat "$work/channel.log" >&2
	fail "xymond_channel kept running after its semaphore set was removed (cpu ticks used: $ticks) -- the fatal semop error spins instead of stopping"
fi

rc=0
wait "$worker" 2>/dev/null || rc=$?

log=$(cat "$work/channel.log")

# Stopped by the intended route, not by dying: a signal death would show up
# here as 128+n, and the diagnostic pins which branch ran.
assert_equal "0" "$rc" "xymond_channel shuts down cleanly on a fatal semaphore error"
assert_contains "cannot continue" "$log" \
	"the fatal semaphore error is reported before exiting"

pass "xymond_channel exits when its semaphore set is removed"
