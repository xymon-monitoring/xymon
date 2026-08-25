#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# A run leaves nothing behind, however it ends.
#
# tests/final/no-leftovers.sh covers the runs that finish. It cannot cover the
# ones that are stopped -- on a signal it never runs -- nor the runner's own
# marker, which it excludes because the marker is still in use while it runs.
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

command -v mktemp >/dev/null 2>&1 || skip "mktemp is needed to watch the marker appear"

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
markers() { find "$1" -maxdepth 1 -name 'xymon-runmarker.*' 2>/dev/null; }

# --- a run that finishes with a failing test ---------------------------------
d="$work/fail"; tmp="$d/tmp"; maketree "$d" failing; mkdir -p "$tmp"
set +e
( cd "$d" && TMPDIR="$tmp" ./tests/testsuite >"$work/fail.log" 2>&1 )
rc=$?
set -e
[ "$rc" -ne 0 ] || fail \
	"the fake suite reported success with a failing test in it, so this case is
not the one it claims to be testing"
left=$(markers "$tmp")
[ -z "$left" ] || fail \
	"a run that ended in failure left its marker behind:
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
	while [ "$i" -lt 100 ] && [ -z "$(markers "$tmp")" ]; do sleep 0.1; i=$((i + 1)); done
	[ -n "$(markers "$tmp")" ] || fail \
		"the runner never created a marker, so signalling it proves nothing about
whether it disposes of one"

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
	[ "$rc" -eq "$expect" ] || errline \
		"SIG$sig left exit status $rc, not $expect; the run ended, but perhaps not
through the trap that is meant to end it"

	left=$(markers "$tmp")
	[ -z "$left" ] || fail \
		"a run stopped with SIG$sig left its marker behind:
$left
Nothing later removes it -- the next run makes its own -- and no-leftovers.sh
cannot catch this, because on this path it never runs."
done

pass "a run disposes of what it made whether it fails, is interrupted, or is terminated"
