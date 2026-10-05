#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/xymond-start-deadline.sh
#
# start_xymond() gives up on its wall-clock deadline, however long each
# readiness probe takes.
#
# The probe is a "ping" through the xymon client, and its cost depends on the
# platform: a port held by a socket that is bound but not listening refuses the
# connection at once on Linux, while the BSDs drop it and the client waits out
# its timeout. A budget counted in probe attempts -- a hundred per startup --
# then came to far more than any job allows, and xymond-port-retry.sh ran for
# hours on NetBSD and OpenBSD with nothing reported.
#
# Linux never produces a slow probe on its own, so this one is made slow on
# purpose: the client is a stub that waits a second and fails, as a dropped
# connection does. xymond itself is real and stays up, so only the deadline
# can end the wait.
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
}

# The slow probe: a second's wait, then failure, every time.
printf '#!/bin/sh\nsleep 1\nexit 1\n' > "$work/slow-client"
chmod +x "$work/slow-client"
XYMONCLIENT="$work/slow-client"

deadline=3
guard=20

start=$(date +%s)
( XYMOND_START_TIMEOUT=$deadline start_xymond ) > "$work/start.out" 2>&1 &
starter=$!
register_cleanup "kill $starter 2>/dev/null || true"
register_cleanup "[ -s '$work/xymond.pid' ] && kill \$(cat '$work/xymond.pid') 2>/dev/null || true"

while kill -0 "$starter" 2>/dev/null; do
	if [ $(( $(date +%s) - start )) -ge "$guard" ]; then
		kill "$starter" 2>/dev/null || true
		fail "start_xymond was still waiting after ${guard}s with XYMOND_START_TIMEOUT=${deadline} and a probe that takes 1s -- its budget counts probes, not time, so a slow probe makes it unbounded"
	fi
	sleep 0.2
done

rc=0; wait "$starter" || rc=$?
elapsed=$(( $(date +%s) - start ))

[ "$rc" -ne 0 ] || fail "start_xymond reported success, though no probe ever answered"
grep -q "did not start within ${deadline}s" "$work/start.out" \
	|| fail "start_xymond gave up after ${elapsed}s without naming its deadline: $(tail -3 "$work/start.out")"

pass "start_xymond gives up on its ${deadline}s deadline (after ${elapsed}s) when every probe takes 1s"
