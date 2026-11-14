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
#
# Gone is what process_running says, not kill -0: a process that has exited
# and that no parent reaps stays in the table as a zombie, and kill -0 still
# answers for it. That is what an orphan becomes where PID 1 does not reap --
# a container whose first process is not an init (#866) -- and stop_process
# then waited its full eleven seconds for a process that had already exited.
# Checked with a zombie made here: its parent replaces itself with a sleep,
# which never waits for it.

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
process_running "$other" || fail "process_running does not see a process that runs"
stop_process "$other"
process_running "$other" \
	&& fail "stop_process returned while a process that is not its child, still exiting, was running"

# Exited and never reaped. Where ps tells no state, process_running can only
# ask kill -0, and there is nothing to check.
if ps -o stat= -p $$ >/dev/null 2>&1 || ps -o s= -p $$ >/dev/null 2>&1; then
	sh -c "sh -c 'exit 0' & echo \$! >'$work/zombie'; exec sleep 30" &
	holder=$!
	register_cleanup "stop_process -KILL $holder"
	for _ in $(seq 1 50); do
		[ -s "$work/zombie" ] && break
		sleep 0.1
	done
	zombie=$(cat "$work/zombie")
	sleep 0.3
	kill -0 "$zombie" 2>/dev/null || fail "the exited process is not in the table: no zombie to check with"
	process_running "$zombie" && fail "process_running takes an exited, unreaped process for a running one"
	process_running "$holder" || fail "process_running does not see the zombie's parent, which runs"
	t0=$SECONDS
	stop_process "$zombie"
	[ $((SECONDS - t0)) -lt 5 ] \
		|| fail "stop_process waited $((SECONDS - t0)) s for a process that had already exited"
	stop_process "$holder"
fi

stop_process "" 999999999 || fail "stop_process failed on an empty and a dead PID"

pass "stop_process returns once the process has exited, its child or not, reaped or not, and accepts empty and dead PIDs"
