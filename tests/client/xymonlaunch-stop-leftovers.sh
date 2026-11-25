#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/client/xymonlaunch-stop-leftovers.sh
#
# A periodic task may leave something running after it exits: the client
# detaches a vmstat for the five minutes its sample takes. That process is no
# child of xymonlaunch and no task, so when xymonlaunch stopped nothing ended
# it, whatever had started xymonlaunch -- an init script, systemd, launchd or
# runclient.sh.
#
# xymonlaunch starts a periodic task as the leader of a process group of its
# own, and on stopping ends what is left of each such group. This drives the
# real binary and checks, on processes:
#
#   - what a periodic task detached, the task itself long gone, is ended when
#     xymonlaunch stops;
#   - a periodic task still running at that moment is ended with what it
#     started, not alone;
#   - a task that stays up is signalled alone, as before: the process it
#     started is not, so a daemon's worker still ends when the daemon tells
#     it to, with what it holds written.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin XYMONLAUNCH "common/xymonlaunch"

work=$(mktempdir)
cfg="$work/tasks.cfg"
log="$work/launch.log"

# Each helper's sleeper execs its wait, so the pid it records is the one to
# look for afterwards.

# Detaches a sleeper the way the client detaches vmstat, and exits. Once: the
# task comes round again every few seconds, and one sleeper is what is tested.
cat >"$work/detach.sh" <<HELPER
#!/bin/sh
[ -e "$work/detached.pid" ] && exit 0
nohup sh -c 'echo \$\$ >"$work/detached.pid"; exec sleep 300' </dev/null >/dev/null 2>&1 &
exit 0
HELPER

# Still running when xymonlaunch stops, with a child of its own.
cat >"$work/slow.sh" <<HELPER
#!/bin/sh
sh -c 'echo \$\$ >"$work/slow-child.pid"; exec sleep 300' &
echo \$\$ >"$work/slow.pid"
wait
HELPER

# Stays up, as a daemon does, with a worker under it.
cat >"$work/daemon.sh" <<HELPER
#!/bin/sh
sh -c 'echo \$\$ >"$work/worker.pid"; exec sleep 300' &
echo \$\$ >"$work/daemon.pid"
wait
HELPER
chmod +x "$work"/*.sh

# A short INTERVAL, not a long one that would never come round here anyway:
# xymonlaunch first starts a task once its monotonic clock (lib/timing.c)
# reaches the interval, and on Linux that clock counts from boot. A host up
# for less than an hour -- a CI runner -- would not start "INTERVAL 1h" at all.
cat >"$cfg" <<TASKS
[detach]
	CMD $work/detach.sh
	INTERVAL 5

[slow]
	CMD $work/slow.sh
	INTERVAL 5

[daemon]
	CMD $work/daemon.sh
TASKS

wait_for() {
	local deadline=$((SECONDS + $1)); shift
	while [ "$SECONDS" -lt "$deadline" ]; do
		if eval "$@"; then return 0; fi
		sleep 0.2
	done
	return 1
}
pid_of() { cat "$work/$1.pid"; }
# process_running, not kill -0: an orphan that was ended stays a zombie where
# nothing reaps it, and that is a process gone.
alive() { process_running "$(pid_of "$1")"; }
end_helpers() {
	local f
	for f in "$work"/*.pid; do
		[ -f "$f" ] && kill "$(cat "$f")" 2>/dev/null
	done
	return 0
}

"$XYMONLAUNCH" --config="$cfg" --no-daemon --debug >"$log" 2>&1 &
launcher=$!
register_cleanup "stop_process $launcher; end_helpers"

for p in detached slow slow-child daemon worker; do
	wait_for 20 '[ -s "$work/'"$p"'.pid" ]' \
		|| fail "the test's $p process never started: $(tail -n 5 "$log")"
done

# The detaching task has to be seen gone by xymonlaunch, or stopping now would
# find it "still running" and test the second case twice.
wait_for 30 'grep -q "Task detach left processes running" "$log"' \
	|| fail "xymonlaunch did not notice that the task [detach] left a process running when it exited: $(tail -n 5 "$log")"
alive detached \
	|| fail "the detached process went away by itself, before xymonlaunch was stopped: nothing was tested"

kill -TERM "$launcher"
wait_for 20 '! process_running "$launcher"' \
	|| fail "xymonlaunch did not stop on SIGTERM"

wait_for 10 '! alive detached' \
	|| fail "what a periodic task detached is still running after xymonlaunch stopped"
wait_for 10 '! alive slow && ! alive slow-child' \
	|| fail "a periodic task running when xymonlaunch stopped was not ended with the process it started"
wait_for 10 '! alive daemon' \
	|| fail "the task that stays up was not ended when xymonlaunch stopped"

# Looked at last, once the others have had time to go: a signal sent to the
# daemon's group would have ended the worker by now.
alive worker \
	|| fail "the worker of a task that stays up was signalled when xymonlaunch stopped: a daemon's children must be left to the daemon"

pass "stopping xymonlaunch ends what its periodic tasks left or were running, and signals a task that stays up alone"
