#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/xymond/channel-hup-rotate.sh
#
# xymond_channel spends nearly all its time blocked waiting for work, and the
# SIGHUP that asks it to reopen its logfile is exactly what interrupts that
# wait. The handler cannot reopen anything itself -- opening a file is not a
# thing to do from a signal handler -- so it raises a flag, and the wait
# returns EINTR into a branch that continues straight back to waiting. With
# the check placed after the wait, that continue skipped it: an idle channel
# went on writing to the rotated file until traffic happened to arrive, which
# on a quiet channel at rotation time can be a long while.
#
# So the rotation is driven here with no traffic at all, which is the case
# that failed. The harness is the one from channel-fatal-semop.sh: it creates
# the channel's IPC the way xymond does.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
# shellcheck source=tests/lib/build-worker.sh
. "$(dirname "$0")/../lib/build-worker.sh"

require_bin XYMOND_CHANNEL xymond/xymond_channel

work=$(mktempdir)
mkdir -p "$work/home"
log="$work/channel.log"

build_xymond_worker "$work" channel-harness tests/xymond/channel-fatal-semop-harness.c

ids=$(XYMONHOME="$work/home" "$work/channel-harness" create status) \
	|| fail "the harness could not create the status channel"
eval "$ids"
[ -n "${SEMID:-}" ] && [ -n "${SHMID:-}" ] || fail "the harness did not report the channel ids"
register_cleanup "'$work/channel-harness' rmall '$SHMID' '$SEMID' >/dev/null 2>&1 || true"

# No --logfile on the command line: a launcher-started task is given its log
# through the launcher's LOGFILE, and learns the path from the environment.
# That is the case rotation has to work in.
XYMONHOME="$work/home" XYMONLAUNCH_LOGFILENAME="$log" \
	"$XYMOND_CHANNEL" --channel=status /bin/cat >"$log" 2>&1 &
worker=$!
register_cleanup "kill -9 $worker 2>/dev/null || true"

attached=no
for _ in $(seq 1 100); do
	[ "$("$work/channel-harness" clients "$SEMID" 2>/dev/null || echo 0)" -ge 1 ] \
		&& { attached=yes; break; }
	sleep 0.1
done
[ "$attached" = yes ] || { cat "$log" >&2; fail "xymond_channel never attached to the channel"; }

# It is now blocked in the wait, and nothing will be sent to it.
mv "$log" "$work/channel.log.1"
kill -HUP "$worker"

# What is asserted is where the process's stdout points, not what the file
# holds: xymond_channel never calls setvbuf(), so with stdout on a file its
# output stays in the buffer, and a reopened log can legitimately be empty for
# a long time. The descriptor moves the moment reopen_file() runs.
if [ -r "/proc/$worker/fd/1" ]; then
	moved=no
	for _ in $(seq 1 100); do
		[ "$(readlink "/proc/$worker/fd/1")" = "$log" ] && { moved=yes; break; }
		sleep 0.1
	done
	[ "$moved" = yes ] || {
		cat "$work/channel.log.1" >&2
		fail "an idle xymond_channel kept writing to the rotated file after SIGHUP: its stdout is still $(readlink "/proc/$worker/fd/1")"
	}
else
	# No /proc: the recreated file is the observable left, and only the
	# reopen can recreate that path -- nothing else writes there.
	reopened=no
	for _ in $(seq 1 100); do
		[ -e "$log" ] && { reopened=yes; break; }
		sleep 0.1
	done
	[ "$reopened" = yes ] || {
		cat "$work/channel.log.1" >&2
		fail "an idle xymond_channel did not reopen its logfile on SIGHUP"
	}
fi

pass "an idle xymond_channel reopens its logfile on SIGHUP, without waiting for traffic"
