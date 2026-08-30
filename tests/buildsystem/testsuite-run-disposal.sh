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

# tree KIND -> a fake suite root. "slow" sleeps so a signal can arrive mid-run;
# "failing" returns non-zero so the run ends badly rather than early.
maketree() {
	local dir=$1 kind=$2
	mkdir -p "$dir/tests/dummy"
	cp "$ROOT/tests/testsuite" "$dir/tests/testsuite"
	chmod +x "$dir/tests/testsuite"
	case $kind in
	  slow)    printf '#!/bin/sh\nsleep 20\n'        > "$dir/tests/dummy/t.sh" ;;
	  failing) printf '#!/bin/sh\nexit 1\n'          > "$dir/tests/dummy/t.sh" ;;
	esac
	chmod +x "$dir/tests/dummy/t.sh"
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
	d="$work/sig$sig"; tmp="$d/tmp"; maketree "$d" slow; mkdir -p "$tmp"
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

	kill -"$sig" "$runner" 2>/dev/null || fail "could not send SIG$sig to the runner"
	expect=130; [ "$sig" = TERM ] && expect=143

	# It finishes the sleeping test first, then runs the trap.
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
maketree "$d1" slow; maketree "$d2" slow
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

kill -TERM $p1 $p2 2>/dev/null || :
i=0
while [ "$i" -lt 300 ] && { kill -0 $p1 2>/dev/null || kill -0 $p2 2>/dev/null; }; do
	sleep 0.1; i=$((i + 1))
done
left=$(runroots "$tmp")
[ -z "$left" ] || fail \
	"two runs stopped together left a directory behind:
$left"

pass "a run disposes of what it made when it fails, is interrupted or is terminated -- and two runs sharing a TMPDIR keep out of each other's way"
