#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/xymond/channel-setup-failure-releases-ipc.sh
#
# A xymond whose startup fails partway through its channels releases the
# channels it had already set up (#578).
#
# It used to return at the failing channel and leave the earlier ones'
# shared memory and semaphore sets allocated until ipcrm or a reboot. Each
# channel holds a semaphore set, a xymond holds nine, and NetBSD and OpenBSD
# allow ten in all, so once two sets had leaked -- from one start failing at
# its third channel or later, or from several failing earlier -- the next
# start failed too, with "Could not get sem: No space left on device".
#
# The real xymond is made to fail at its second channel, stachg: the harness
# leaves a one-semaphore set on stachg's key, which setup_channel() cannot
# take over. status, the first channel, is set up before that, and is the
# one that must be gone afterwards.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
# shellcheck source=tests/lib/build-worker.sh
. "$(dirname "$0")/../lib/build-worker.sh"
# shellcheck source=tests/lib/xymond-daemon.sh
. "$(dirname "$0")/../lib/xymond-daemon.sh"

require_bin XYMOND xymond/xymond
# free_port() probes candidate ports with the client.
require_bin XYMONCLIENT common/xymon
require_shm_segments "$(grep -c 'setup_channel(C_[A-Z_]*, CHAN_MASTER)' "$(find_root)/xymond/xymond.c")"

work=$(mktempdir)
# setup_channel() keys the IPC off ftok(XYMONHOME), so a per-test directory
# keeps these keys apart from a real Xymon on the same host.
mkdir -p "$work/home/etc" "$work/home/tmp" "$work/home/www"
printf 'page test Test\n127.0.0.1 testhost.example.com # conn\n' > "$work/hosts.cfg"

require_cfg XYMONSERVER_CFG xymond/etcfiles/xymonserver.cfg
sed -e 's|^XYMONHOME=.*|XYMONHOME="'"$work"'/home"|' \
    -e 's|^XYMONTMP=.*|XYMONTMP="'"$work"'/home/tmp"|' \
	"$XYMONSERVER_CFG" > "$work/xymonserver.cfg"

build_xymond_worker "$work" ipc-harness tests/xymond/channel-setup-failure-harness.c
harness() { XYMONHOME="$work/home" "$work/ipc-harness" "$@"; }

register_cleanup "XYMONHOME='$work/home' '$work/ipc-harness' release status >/dev/null 2>&1 || true"
register_cleanup "XYMONHOME='$work/home' '$work/ipc-harness' release stachg >/dev/null 2>&1 || true"

[ -z "$(harness held status)" ] || fail "the status channel's key already holds IPC before xymond ran, so the check below would say nothing"
harness block stachg || fail "the harness could not put a one-semaphore set on the stachg key"

port=$(free_port) || fail "no free port for xymond"
rc=0
"$XYMOND" --no-daemon --listen="127.0.0.1:$port" \
	--hosts="$work/hosts.cfg" --env="$work/xymonserver.cfg" \
	--pidfile="$work/xymond.pid" --checkpoint-file="$work/chk.out" \
	> "$work/xymond.log" 2>&1 || rc=$?

# Non-vacuity: the failure must be at stachg, after status was set up.
[ "$rc" -ne 0 ] || fail "xymond started although its stachg channel could not be set up"
grep -q 'Cannot setup stachg channel' "$work/xymond.log" ||
	fail "xymond did not fail at the stachg channel, so status was never set up for it to release: $(tail -3 "$work/xymond.log")"

left=$(harness held status | tr '\n' ' ')
[ -z "$left" ] ||
	fail "xymond failed at stachg and left the status channel it had set up allocated ($left) -- it stays until ipcrm or a reboot"

pass "a xymond that fails at its second channel releases the first"
