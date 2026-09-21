#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# A run leaves nothing behind, however it ends.
#
# no-leftovers.sh covers the runs that finish, and only what is inside the run
# directory. The endings it cannot see are here: a failing run, SIGINT, SIGTERM
# -- and the run directory itself, which is still in use while it runs.
# So those are here: a failing run, SIGINT, SIGTERM. SIGKILL is out of scope,
# since no trap runs.
#
# Against a fake tree of one test: the runner discovers every executable
# tests/**/*.sh under its root, this file included, and would recurse.
#
# The outcome is asserted, not which trap delivers it -- under dash, EXIT alone
# already covers the signal paths here.
set -eu
. "$(dirname "$0")/../lib/assert.sh"
ROOT=$(find_root)

command -v mktemp >/dev/null 2>&1 || skip "mktemp is needed to watch the run directory appear"

work=$(mktempdir); register_cleanup "rm -rf '$work'"

# tree KIND [STARTED STOPFILE] -> a fake suite root. "slow" announces itself in
# STARTED and then blocks until STOPFILE appears, so a signal can arrive while
# it is running and the run ends as soon as it has; "failing" returns non-zero
# so the run ends badly rather than early.
#
# The slow test used to sleep 20. That was not padding: it had to outlast the
# 10s the caller spends waiting for the run directory, and fit inside the 30s
# it then waits for the runner to go, so 20 was the midpoint of the only window
# available. Measured, it bought about 20s of tolerance -- a fake test that
# sleeps 5 already fails once the caller is held off for 8s.
#
# But it was a guess at how slow the machine would be, and the runner finishes
# the running test before its trap fires, so the whole sleep is paid after the
# signal. Waiting on a file instead removes the guess: the caller creates
# STOPFILE immediately after kill(1) returns, so the test ends when the signal
# has been queued rather than when a timer says it probably has. Under the same
# 25s of simulated load where sleep 20 fails, this waits and still passes. The
# 30s cap is a deadlock guard, not the expected duration.
#
# STARTED is what makes the signal cases mean what they say. Waiting for the
# run directory is not enough: the runner creates it before it runs anything,
# so a signal sent on that cue can be handled before the fake test has begun,
# and the case then proves only that a run with nothing in it cleans up.
maketree() {
	local dir=$1 kind=$2 started=${3:-} stop=${4:-}
	mkdir -p "$dir/tests/dummy"
	cp "$ROOT/tests/testsuite" "$dir/tests/testsuite"
	chmod +x "$dir/tests/testsuite"
	case $kind in
	  slow)
		# Both paths are quoted in the generated script: this suite is run
		# from directories with spaces in them, and the runner itself says so
		# at the loop that reads the test list.
		#
		# Single quotes here are deliberate -- $i belongs to the generated
		# script, not to this one. An empty path would collapse "[ ! -f x ]"
		# to a one-argument test, which is false, so the slow test would
		# return at once and the signal would land after it: the run would
		# leave through the EXIT trap while the case claimed to have
		# exercised the signal one.
		[ -n "$started" ] && [ -n "$stop" ] || fail \
			"maketree was asked for a slow tree without both marker paths, so the
test would not have been slow and the signal case would have proved nothing"
		printf '#!/bin/sh\n: > "%s"\ni=0\nwhile [ $i -lt 300 ] && [ ! -f "%s" ]; do sleep 0.1; i=$((i+1)); done\n' \
			"$started" "$stop" > "$dir/tests/dummy/t.sh" ;;
	  failing) printf '#!/bin/sh\nexit 1\n'          > "$dir/tests/dummy/t.sh" ;;
	esac
	chmod +x "$dir/tests/dummy/t.sh"
}

# waitfile PATH SECONDS -- true once PATH exists.
waitfile() {
	local i=0 n=$(( ${2} * 10 ))
	while [ "$i" -lt "$n" ] && [ ! -f "$1" ]; do sleep 0.1; i=$((i + 1)); done
	[ -f "$1" ]
}

# One directory per run, removed however the run ends.
runroots() { find "$1" -maxdepth 1 -name 'xymon-run.*' 2>/dev/null; }

# --- a run that finishes with a failing test ---------------------------------
d="$work/fail"; tmp="$d/tmp"; maketree "$d" failing; mkdir -p "$tmp"
set +e
( cd "$d" && TMPDIR="$tmp" ./tests/testsuite >"$work/fail.log" 2>&1 )
rc=$?
set -e
[ "$rc" -ne 0 ] || fail \
	"the fake suite reported success with a failing test in it, so this case is
not the one it claims to be testing"
left=$(runroots "$tmp")
[ -z "$left" ] || fail \
	"a run that ended in failure left its run directory behind:
$left
A failing run is the one somebody re-runs, so its leftovers are the ones that
accumulate fastest."

# --- a run stopped by a signal, one case per signal ---------------------------
for sig in INT TERM; do
	d="$work/sig$sig"; tmp="$d/tmp"
	started="$work/started.$sig"; stop="$work/stop.$sig"
	maketree "$d" slow "$started" "$stop"; mkdir -p "$tmp"
	# exec, so $! is the runner and not a wrapper subshell. Without it the
	# signal never reaches the runner, and the exit status is the same 143 the
	# TERM trap sets -- indistinguishable from a missing trap.
	( cd "$d" && TMPDIR="$tmp" exec ./tests/testsuite >"$work/$sig.log" 2>&1 ) &
	runner=$!
	register_cleanup "kill -9 $runner 2>/dev/null || :"

	i=0
	while [ "$i" -lt 100 ] && [ -z "$(runroots "$tmp")" ]; do sleep 0.1; i=$((i + 1)); done
	[ -n "$(runroots "$tmp")" ] || fail \
		"the runner never created its run directory, so signalling it proves nothing
about whether it disposes of one"

	waitfile "$started" 30 || fail \
		"the fake test never started, so SIG$sig would have reached a run that had
nothing running in it -- which is not the case this is here to cover"

	kill -"$sig" "$runner" 2>/dev/null || fail "could not send SIG$sig to the runner"
	# The signal is queued behind the running test, so release the test now
	# that it has been queued.
	: > "$stop"
	expect=130; [ "$sig" = TERM ] && expect=143

	# It finishes the running test first, then runs the trap.
	i=0
	while [ "$i" -lt 300 ] && kill -0 "$runner" 2>/dev/null; do sleep 0.1; i=$((i + 1)); done
	kill -0 "$runner" 2>/dev/null && fail \
		"the runner ignored SIG$sig and was still going 30s later; a suite that
cannot be stopped is its own problem, and the cleanup of a run that has not
ended cannot be checked"
	set +e; wait "$runner"; rc=$?; set -e
	# Reported, not asserted: the status a trap leaves is not portable -- bash
	# 3.2 reports 0 for both signals where bash 5 reports 130 and 143. The
	# cleanup below is the invariant, and it does not vary by shell.
	[ "$rc" -eq "$expect" ] || printf '  note: SIG%s left exit status %s, not %s (shell-dependent)\n' \
		"$sig" "$rc" "$expect" >&2

	left=$(runroots "$tmp")
	[ -z "$left" ] || fail \
		"a run stopped with SIG$sig left its run directory behind:
$left
Nothing later removes it -- the next run makes its own -- and no-leftovers.sh
cannot catch this, because on this path it never runs."
done

# --- two runs at once ---------------------------------------------------------
# Measured before the per-run directory: run A failed, naming run B's live
# files as leftovers.
d1="$work/par1"; d2="$work/par2"; tmp="$work/partmp"; mkdir -p "$tmp"
stop="$work/stop.par"
maketree "$d1" slow "$work/started.par1" "$stop"
maketree "$d2" slow "$work/started.par2" "$stop"
( cd "$d1" && TMPDIR="$tmp" exec ./tests/testsuite >"$work/par1.log" 2>&1 ) &
p1=$!
( cd "$d2" && TMPDIR="$tmp" exec ./tests/testsuite >"$work/par2.log" 2>&1 ) &
p2=$!
register_cleanup "kill -9 $p1 $p2 2>/dev/null || :"

i=0
while [ "$i" -lt 100 ] && [ "$(runroots "$tmp" | wc -l | tr -d ' ')" -lt 2 ]; do
	sleep 0.1; i=$((i + 1))
done
[ "$(runroots "$tmp" | wc -l | tr -d ' ')" -ge 2 ] || fail \
	"two runs sharing one TMPDIR did not end up with a directory each, so they
are still sharing state and each can see the other's files:
$(runroots "$tmp")"

waitfile "$work/started.par1" 30 && waitfile "$work/started.par2" 30 || fail \
	"one of the two runs never started its test, so terminating them proves nothing
about two live runs keeping out of each other's way"

kill -TERM $p1 $p2 2>/dev/null || :
: > "$stop"
i=0
while [ "$i" -lt 300 ] && { kill -0 $p1 2>/dev/null || kill -0 $p2 2>/dev/null; }; do
	sleep 0.1; i=$((i + 1))
done
left=$(runroots "$tmp")
[ -z "$left" ] || fail \
	"two runs stopped together left a directory behind:
$left"

pass "a run disposes of what it made when it fails, is interrupted or is terminated -- and two runs sharing a TMPDIR keep out of each other's way"
