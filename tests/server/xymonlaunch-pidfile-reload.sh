#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/xymonlaunch-pidfile-reload.sh
#
# A task with PIDFILE has its pid written by the forked child, and xymonlaunch
# removes that file when the task leaves the config or its path changes --
# "Always remove pidfn, even if it wasn't running", as the code puts it. But
# the re-read clears every task's settings first, so by then twalk->pidfile is
# no longer the path the file was written to: NULL for a deleted task, so
# nothing was removed at all, and the *new* path for a changed one, so the
# unlink hit a name not yet written while the old file stayed behind holding a
# pid the system may hand to something else.
#
# Hence the two cases below: a task removed from the file, and a PIDFILE that
# moved.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin XYMONLAUNCH "common/xymonlaunch"

work=$(mktempdir)
cfg="$work/tasks.cfg"
log="$work/launch.log"

cat >"$work/beat.sh" <<EOF
#!/bin/sh
while :; do echo tick >>"$work/beat"; sleep 1; done
EOF
chmod +x "$work/beat.sh"

# write_config <pidfile-path-or-empty>
#
# Every write carries a comment one character longer than the last. xymonlaunch
# decides whether to re-read from st_mtime -- in whole seconds -- and the size,
# so rewriting the same content inside one second is invisible to it and the
# reload this test waits for never happens. A revision *number* is not enough:
# "# rev 7" and "# rev 8" are the same length.
rev=0
write_config() {
	rev=$((rev + 1))
	if [ -n "$1" ]; then
		printf '#%*s\n[beat]\n\tCMD %s\n\tPIDFILE %s\n' "$rev" "" "$work/beat.sh" "$1" >"$cfg"
	else
		printf '#%*s\n[other]\n\tCMD /bin/true\n\tINTERVAL 1h\n' "$rev" "" >"$cfg"
	fi
}

wait_for() {
	local deadline=$((SECONDS + $1)); shift
	while [ "$SECONDS" -lt "$deadline" ]; do
		if eval "$@"; then return 0; fi
		sleep 0.2
	done
	return 1
}
loadcount() { grep -c 'Loading tasklist configuration' "$log" || true; }
reload() {
	local n; n=$(loadcount)
	kill -HUP "$launcher"
	wait_for 15 '[ "$(loadcount)" -gt '"$n"' ]' \
		|| fail "xymonlaunch did not re-read its config after SIGHUP"
}

write_config "$work/beat.pid"
"$XYMONLAUNCH" --config="$cfg" --no-daemon >"$log" 2>&1 &
launcher=$!
register_cleanup "kill $launcher 2>/dev/null || true; pkill -f ${work}/beat.sh 2>/dev/null || true"

wait_for 20 '[ -s "$work/beat.pid" ]' || fail "the task pidfile was never written: $(cat "$log")"

# It must hold the supervised child, not the launcher: the two files are
# written by different code paths, and a test that accepted either would pass
# while the task's own pidfile was never touched.
pid=$(cat "$work/beat.pid")
if [ "$pid" = "$launcher" ]; then
	fail "the task pidfile holds xymonlaunch's own pid, so it is not the task's"
fi
wait_for 20 '[ -s "$work/beat" ]' || fail "the supervised task never started: $(cat "$log")"

# ---- the task leaves the config --------------------------------------------
write_config ''
reload
wait_for 15 '[ ! -e "$work/beat.pid" ]' \
	|| fail "the pidfile of a deleted task was left behind, holding a pid the system may reuse"

# ---- the path moves --------------------------------------------------------
# Back with the original path, then move it. The old name must go, and the
# restarted task must write the new one.
write_config "$work/beat.pid"
reload
wait_for 20 '[ -s "$work/beat.pid" ]' || fail "the task did not come back: $(cat "$log")"

write_config "$work/moved.pid"
reload
wait_for 15 '[ ! -e "$work/beat.pid" ]' \
	|| fail "a PIDFILE that moved left its old file behind"
wait_for 20 '[ -s "$work/moved.pid" ]' \
	|| fail "the task did not write its pidfile at the new path: $(cat "$log")"

# ---- the task exits on its own ---------------------------------------------
# A task with a long interval that ends by itself: it runs, exits, is reaped,
# and does not come back for an hour. Its pidfile named a process that no
# longer exists, and that number is one the system is free to hand to someone
# else -- which is what makes a stale pidfile worse than a missing one.
#
# Driven from a second launcher whose config already holds the task, rather
# than by rewriting this one's. A re-read is not something a test can ask for
# on demand: SIGHUP only clears the next-load timer, and load_config() still
# returns early unless stackfmodified() sees a change -- which it decides on
# st_mtime, in whole seconds. Two rewrites inside one second are invisible, so
# on a fast machine the task this case needs was never created at all.
#
# INTERVAL is 30 seconds and not an hour, which matters more than it looks:
# xymonlaunch reads the clock with gettimer(), i.e. CLOCK_MONOTONIC, so "now"
# is the machine's uptime. A new task has laststart 0, and the start condition
# is now >= laststart + interval -- so INTERVAL 1h means "not until this host
# has been up an hour". On a long-lived machine that is invisible; on a freshly
# booted CI runner the task simply never starts. Thirty seconds is long enough
# that the task does not come back while its absence is being checked.
oneshot="$work/oneshot.cfg"
printf '[oneshot]\n\tCMD /bin/sleep 5\n\tINTERVAL 30\n\tPIDFILE %s\n' "$work/oneshot.pid" >"$oneshot"
"$XYMONLAUNCH" --config="$oneshot" --no-daemon >"$work/oneshot.log" 2>&1 &
launcher2=$!
register_cleanup "kill $launcher2 2>/dev/null || true"

wait_for 30 '[ -e "$work/oneshot.pid" ]' \
	|| fail "the one-shot task never wrote its pidfile: $(cat "$work/oneshot.log")"
wait_for 30 '[ ! -e "$work/oneshot.pid" ]' \
	|| fail "the pidfile of a task that exited was left behind, naming a dead pid"
kill "$launcher2" 2>/dev/null || true

# ---- a config read that stopped short --------------------------------------
# The re-read clears every task's settings before parsing, and a read that ends
# mid-file is put back from the copy taken beforehand (#348). That restore names
# the fields one by one, so a field added later is silently not restored: the
# task keeps running but loses its PIDFILE, and nothing removes the file when
# the task later leaves the config.
# The premise: the task is back on beat.pid and the file is there. The case
# before this one left it writing moved.pid, and an "is it gone" assertion is
# satisfied by a file that was never there.
write_config "$work/beat.pid"
reload
wait_for 20 '[ -s "$work/beat.pid" ]' || fail "the task is not writing beat.pid: $(cat "$log")"

b=$(wc -l <"$work/beat")
printf '# short read\n[beat]\n' >"$cfg"
reload
wait_for 20 '[ "$(wc -l <"$work/beat")" -gt '"$b"' ]' \
	|| fail "the task did not survive the short read: $(cat "$log")"

write_config ''
reload
wait_for 20 '[ ! -e "$work/beat.pid" ]' \
	|| fail "after a short read the task kept running but lost its PIDFILE, so nothing removed the file when it left the config"

write_config "$work/beat.pid"
reload
wait_for 20 '[ -s "$work/beat.pid" ]' || fail "the task did not come back: $(cat "$log")"

# ---- xymonlaunch itself stops ----------------------------------------------
# Nothing reaps the tasks from the shutdown path, so the unlink done at reap
# time cannot run there: their pidfiles outlived the whole launcher, naming
# pids that die moments later.
write_config "$work/beat.pid"
reload
wait_for 20 '[ -s "$work/beat.pid" ]' || fail "the task did not come back before shutdown: $(cat "$log")"

kill -TERM "$launcher"
wait_for 20 '! kill -0 "$launcher" 2>/dev/null' || fail "xymonlaunch would not stop"
[ ! -e "$work/beat.pid" ] \
	|| fail "xymonlaunch left its tasks' pidfiles behind when it stopped, naming pids that are gone"

pass "a task pidfile is removed when the task leaves the config, moves, exits, or the launcher stops"
