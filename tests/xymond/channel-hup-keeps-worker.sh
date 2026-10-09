#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/xymond/channel-hup-keeps-worker.sh
#
# A HUP to xymond_channel reopens the channel's own log and goes no further.
# The worker's log is rotated by the @@logrotate message xymond posts to every
# channel, which the channel passes down the pipe; a raw SIGHUP sent on to the
# worker would kill every worker that installs no HUP handler, as
# xymond_filestore and xymond_distribute do not, and drop the channel's data.
#
# The worker here is /bin/cat, which installs none: the HUP's default action
# ends it. The channel runs against the harness from channel-fatal-semop.sh,
# as in channel-hup-rotate.sh. It starts its worker only once it has a
# message to hand it, and only a worker it has delivered to was ever sent the
# HUP, so one message is posted first. The worker must be the same live
# process after the channel has handled the HUP.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
# shellcheck source=tests/lib/build-worker.sh
. "$(dirname "$0")/../lib/build-worker.sh"

require_bin XYMOND_CHANNEL xymond/xymond_channel
# ps finds the worker the channel forked; a minimal container may not have it.
command -v ps >/dev/null 2>&1 \
	|| skip "ps not available (needed to find the channel's worker)"

work=$(mktempdir)
mkdir -p "$work/home"
log="$work/channel.log"

build_xymond_worker "$work" channel-harness tests/xymond/channel-fatal-semop-harness.c

ids=$(XYMONHOME="$work/home" "$work/channel-harness" create status) \
	|| fail "the harness could not create the status channel"
eval "$ids"
[ -n "${SEMID:-}" ] && [ -n "${SHMID:-}" ] || fail "the harness did not report the channel ids"
register_cleanup "'$work/channel-harness' rmall '$SHMID' '$SEMID' >/dev/null 2>&1 || true"

XYMONHOME="$work/home" XYMONLAUNCH_LOGFILENAME="$log" \
	"$XYMOND_CHANNEL" --channel=status /bin/cat >"$log" 2>&1 &
channel=$!
register_cleanup "kill -9 $channel 2>/dev/null || true"

attached=no
for _ in $(seq 1 100); do
	[ "$("$work/channel-harness" clients "$SEMID" 2>/dev/null || echo 0)" -ge 1 ] \
		&& { attached=yes; break; }
	sleep 0.1
done
[ "$attached" = yes ] || { cat "$log" >&2; fail "xymond_channel never attached to the channel"; }

"$work/channel-harness" post "$SHMID" "$SEMID" "@@status#1/test.host|0.000000|127.0.0.1|test.host|cpu|green" \
	|| fail "the harness could not post a message to the channel"

# The worker is the channel's child; ps -o with these three fields is POSIX.
worker_of() {
	ps -A -o pid= -o ppid= -o comm= | awk -v p="$1" '$2 == p && $3 ~ /cat$/ { print $1; exit }'
}
worker=""
for _ in $(seq 1 100); do
	worker=$(worker_of "$channel")
	[ -n "$worker" ] && break
	sleep 0.1
done
[ -n "$worker" ] || { cat "$log" >&2; fail "xymond_channel never started its worker"; }
register_cleanup "kill -9 $worker 2>/dev/null || true"

# The reopen recreates the log, which says the channel has handled the HUP:
# a HUP sent on to the worker goes out in the same pass, before the wait.
mv "$log" "$work/channel.log.1"
kill -HUP "$channel"
reopened=no
for _ in $(seq 1 100); do
	[ -e "$log" ] && { reopened=yes; break; }
	sleep 0.1
done
[ "$reopened" = yes ] || { cat "$work/channel.log.1" >&2; fail "xymond_channel did not reopen its logfile on SIGHUP"; }
sleep 1

kill -0 "$worker" 2>/dev/null \
	|| fail "xymond_channel passed its HUP on to the worker, which has no HUP handler and died: worker pid $worker is gone"

pass "a HUP to xymond_channel reopens its log and leaves a worker without a HUP handler running"
