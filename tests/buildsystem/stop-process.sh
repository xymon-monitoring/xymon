#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/buildsystem/stop-process.sh
#
# stop_process (tests/lib/assert.sh) returns only once the process it
# signalled has exited. A cleanup that only signals returns while the process
# still runs, and the temp dir removed next is written into meanwhile: xymond
# writes its checkpoint as it exits, rm -rf fails on the new file, and the dir
# is left behind (tests/final/no-leftovers.sh then fails, on a slow lane).
#
# Checked with a process that takes a second to exit after TERM, as xymond
# does while it writes: started as this shell's child, and started as no
# child of it, from a pidfile. After stop_process each is gone. Then empty
# and already dead PIDs are accepted, as a cleanup for something that never
# started needs.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

work=$(mktempdir)
# Ends by itself after 30 s, so nothing outlives the test if stop_process breaks.
slow='trap "sleep 1; exit 0" TERM; n=0; while [ $n -lt 300 ]; do sleep 0.1; n=$((n + 1)); done'

sh -c "$slow" &
child=$!
sleep 0.2
stop_process "$child"
kill -0 "$child" 2>/dev/null \
	&& fail "stop_process returned while its child, still exiting, was running"

# Not a child: started by a shell that is gone, found through its pidfile.
sh -c "sh -c '$slow' & echo \$! >'$work/pid'"
other=$(cat "$work/pid")
sleep 0.2
stop_process "$other"
kill -0 "$other" 2>/dev/null \
	&& fail "stop_process returned while a process that is not its child, still exiting, was running"

stop_process "" 999999999 || fail "stop_process failed on an empty and a dead PID"

pass "stop_process returns once the process has exited, its child or not, and accepts empty and dead PIDs"
