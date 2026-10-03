#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/xymonlaunch-delay-reload.sh
#
# A config re-read clears every task's settings before parsing, so that a
# keyword removed from tasks.cfg goes back to its default. DELAY and FAILDELAY
# were set only where a task is first created, never in that clearing loop, so
# their values outlived the lines that asked for them: a task configured once
# with DELAY 3600 kept waiting an hour after the line was gone.
#
# DELAY is the half driven here, because it is observable in seconds: a task
# with NEEDS does not start until its dependency has been running DELAY seconds
# (xymonlaunch.c:919). FAILDELAY takes the same reset but is not driven here:
# showing it needs a task on hold first (six failed starts, see
# xymonlaunch-faildelay.sh) and then a re-read, so it is stated here rather
# than left to be assumed.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin XYMONLAUNCH "common/xymonlaunch"

work=$(mktempdir)
cfg="$work/tasks.cfg"
log="$work/launch.log"

for t in anchor follower control; do
	cat >"$work/$t.sh" <<EOF
#!/bin/sh
date +%s >"$work/$t.start"
while :; do echo tick >>"$work/$t"; sleep 1; done
EOF
	chmod +x "$work/$t.sh"
done

# write_config <delay-line>
write_config() {
	cat >"$cfg" <<EOF
[anchor]
	CMD $work/anchor.sh

[follower]
	NEEDS anchor
	CMD $work/follower.sh
$1

[control]
	NEEDS anchor
	CMD $work/control.sh
EOF
}

# Poll rather than sleep a fixed time, so a slow machine gets its chance and a
# fast one does not pay for it.
wait_for() {
	local deadline=$((SECONDS + $1)); shift
	while [ "$SECONDS" -lt "$deadline" ]; do
		if eval "$@"; then return 0; fi
		sleep 0.2
	done
	return 1
}
loadcount() { grep -c 'Loading tasklist configuration' "$log" || true; }
# Force the re-read and wait until it has happened, so what follows cannot
# race the signal.
reload() {
	local n; n=$(loadcount)
	kill -HUP "$launcher"
	wait_for 15 '[ "$(loadcount)" -gt '"$n"' ]' \
		|| fail "xymonlaunch did not re-read its config after SIGHUP"
}

write_config '	DELAY 3600'
"$XYMONLAUNCH" --config="$cfg" --no-daemon >"$log" 2>&1 &
launcher=$!
register_cleanup "kill $launcher 2>/dev/null || true; for t in anchor follower control; do pkill -f ${work}/\$t.sh 2>/dev/null || true; done"

wait_for 20 '[ -s "$work/anchor" ]' || fail "the dependency never started: $(cat "$log")"

# The premise, asserted rather than assumed: DELAY 3600 holds the follower
# back. Without it the rest proves nothing -- a follower that starts anyway
# would satisfy the assertion below whatever the reload did.
#
# [control] is the same task without a DELAY line, so the premise is relative
# to this machine at this moment rather than to a number of seconds: waiting
# for the control to start is the proof that a dependent was due, and the
# follower still being absent is then about DELAY and not about a slow poll.
wait_for 40 '[ -s "$work/control" ]' \
	|| fail "no dependent started at all, so nothing here is about DELAY: $(cat "$log")"
# [control] has no DELAY line, so it waits the default five seconds after
# [anchor] starts; one second of slack for the whole-second clocks. A lower
# bound, so a slow machine cannot trip it.
gap=$(( $(cat "$work/control.start") - $(cat "$work/anchor.start") ))
[ "$gap" -ge 4 ] || fail "[control] started $gap s after [anchor], not after the default DELAY of 5"
# On the process, not on its output file: both dependents are forked in the
# same scan, so a follower that xymonlaunch started can still have written
# nothing when the control's first line lands. Absent output would read as
# "held back" while it was merely a few milliseconds behind.
if pgrep -f "$work/follower.sh" >/dev/null 2>&1; then
	fail "DELAY 3600 did not hold the follower back, so the reload proves nothing"
fi

# ---- a refused read must not change what is running -------------------------
# A file caught mid-write -- a section whose CMD has not been written yet --
# is refused, "keeping the previous one", so restore_task() must put back the
# DELAY that the re-read reset to its default.
# The DELAY line is absent here on purpose: a file caught mid-write is cut
# somewhere, and the only cut that can show this is one past the line whose
# value has to survive. A refused config that still carries DELAY 3600 sets
# it again while parsing, so the rollback has nothing left to get wrong and
# the case passes whatever restore_task() does.
cat >"$cfg" <<EOF
[anchor]
	CMD $work/anchor.sh

[follower]
	NEEDS anchor
	CMD $work/follower.sh

[control]
	NEEDS anchor
	CMD $work/control.sh

[truncated]
	NEEDS anchor
EOF
reload

grep -q 'Incomplete tasklist configuration' "$log" \
	|| fail "xymonlaunch accepted a config with a task that has no command, so
this case is not the one it claims to be testing: $(cat "$log")"

# The default DELAY is 5s and the dependency started long ago, so a follower
# whose hold was lost is eligible on the very next scan. Waiting for it to
# appear is the failure, not the pass -- hence a short window rather than a
# long one.
if wait_for 15 'pgrep -f "$work/follower.sh" >/dev/null 2>&1'; then
	fail "a refused reload started the follower: DELAY 3600 was replaced by the
default the refused read installed, and the rollback put back every other
field but not this one"
fi

# Now remove the line. Every other keyword reverts to its default on a
# re-read; DELAY must too, or the hour it asked for outlives the asking.
write_config ''
reload

wait_for 40 '[ -s "$work/follower" ]' \
	|| fail "DELAY kept its old value after the line was removed from the config"

# ---- an explicit DELAY 0 survives a dump -----------------------------------
# --dump is how a running config is read back, and it printed DELAY only when
# the value was non-zero -- so "DELAY 0", which means "start as soon as the
# dependency has a pid", came back as the default 5. Zero is the value the old
# non-zero check dropped, so it is the one written.
#
# --dump parses once and returns, so this needs no daemon.
printf '[anchor]\n\tCMD %s\n\n[follower]\n\tNEEDS anchor\n\tCMD %s\n\tDELAY 0\n' \
	"$work/anchor.sh" "$work/follower.sh" >"$work/dump.cfg"
dump=$("$XYMONLAUNCH" --dump --config="$work/dump.cfg" 2>&1) \
	|| fail "xymonlaunch --dump failed: $dump"
assert_contains "DELAY 0" "$dump" \
	"an explicit DELAY 0 is dropped by --dump, so reusing the dump changes what the config means"

# Unit suffixes are multiplied out, and a non-default FAILDELAY is dumped too.
printf '[anchor]\n\tCMD %s\n\n[follower]\n\tNEEDS anchor\n\tCMD %s\n\tDELAY 1m\n\tFAILDELAY 2h\n' \
	"$work/anchor.sh" "$work/follower.sh" >"$work/dump2.cfg"
dump=$("$XYMONLAUNCH" --dump --config="$work/dump2.cfg" 2>&1) \
	|| fail "xymonlaunch --dump failed: $dump"
assert_contains "DELAY 60" "$dump" "DELAY 1m is 60 seconds"
assert_contains "FAILDELAY 7200" "$dump" "FAILDELAY 2h is 7200 seconds"

pass "DELAY: default 5 s; a removed line reverts; a refused read keeps the old value; --dump keeps DELAY 0 and multiplies out units"
