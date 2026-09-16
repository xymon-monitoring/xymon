#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# The suite leaves nothing in the run's temp directory.
#
# #417 was one cache no run removed. This catches the next one, whichever test
# it is. It runs last because the runner holds tests/final/ back, and it looks
# only at this run's own directory -- so nothing another run is doing can be
# mistaken for a leftover.
set -eu
. "$(dirname "$0")/../lib/assert.sh"

[ -n "${__XYMON_TESTS_RUNROOT:-}" ] && [ -d "${__XYMON_TESTS_RUNROOT:-}" ] || skip \
	"no run directory: this check is about what a whole suite run leaves behind,
and there is no run in progress to judge"

work=$(mktempdir); register_cleanup "rm -rf '$work'"

# Everything still here, except this check's own directory.
leftovers=$(find "$__XYMON_TESTS_RUNROOT" -mindepth 1 -maxdepth 1 \
		! -path "$work" 2>/dev/null | sort)

# Non-vacuity: a find that cannot see a directory plainly there would pass on
# any suite at all.
sentinel="$__XYMON_TESTS_RUNROOT/leftover-probe.$$"
mkdir -p "$sentinel"
seen=$(find "$__XYMON_TESTS_RUNROOT" -mindepth 1 -maxdepth 1 -path "$sentinel" 2>/dev/null)
rmdir "$sentinel" 2>/dev/null || rm -rf "$sentinel"
[ -n "$seen" ] || fail \
	"the sweep did not see a directory that was plainly there, so the assertion
below would pass whatever the suite had left"

[ -z "$leftovers" ] || fail \
	"the suite finished and left these in $__XYMON_TESTS_RUNROOT:

$leftovers

A test that makes a temp directory removes it, whether it passed or failed --
register_cleanup in tests/lib/assert.sh does this and is what every other test
uses. Something here did not. The run directory is removed when the run ends,
so this is not a leak on disk; it is a test that would leak into \$TMPDIR the
moment it is run on its own, which is how #417 reached a release."

pass "the suite left nothing behind in its run directory"
