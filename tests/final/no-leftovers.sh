#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# The suite leaves nothing in the temp directory.
#
# #417 was one cache, 2MB of linked CGI that no run removed. This is the check
# that stops the next one: any test that makes a temp directory and forgets it
# is caught, whichever test it is.
#
# Runs last by construction -- the runner holds tests/final/ back -- rather
# than by filename, which a directory added later could displace.
#
# Judged against a marker the runner drops at the start, so what is reported is
# what THIS run left and not what some earlier one did.
set -eu
. "$(dirname "$0")/../lib/assert.sh"

[ -n "${__XYMON_TESTS_RUN_MARKER:-}" ] && [ -e "${__XYMON_TESTS_RUN_MARKER:-}" ] || skip \
	"no run marker: this check is about what a whole suite run leaves behind, and
there is no run in progress to judge"

tmp=${TMPDIR:-/tmp}
work=$(mktempdir); register_cleanup "rm -rf '$work'"

# Everything this run created under the temp directory and did not take away.
# Excluded: our own work directory, which we are still using and do clean up,
# and the runner's own marker file, which it removes after this has run.
leftovers=$(find "$tmp" -maxdepth 1 -newer "$__XYMON_TESTS_RUN_MARKER" \
		-name 'xymon-*' \
		! -path "$work" \
		! -path "${__XYMON_TESTS_RUN_MARKER:-/nonexistent}" \
		2>/dev/null | sort)

# Non-vacuity: a find that cannot see a directory plainly there would let this
# pass on any suite at all, including one that leaked every test.
sentinel="$tmp/xymon-leftover-probe.$$"
mkdir -p "$sentinel"
seen=$(find "$tmp" -maxdepth 1 -newer "$__XYMON_TESTS_RUN_MARKER" -name 'xymon-*' \
		-path "$sentinel" 2>/dev/null)
rmdir "$sentinel" 2>/dev/null || rm -rf "$sentinel"
[ -n "$seen" ] || fail \
	"the sweep did not see a directory that was plainly there, so the assertion
below would pass whatever the suite had left"

[ -z "$leftovers" ] || fail \
	"the suite finished and left these in $tmp:

$leftovers

A test that makes a temp directory removes it, whether it passed or failed --
register_cleanup in tests/lib/assert.sh does this and is what every other test
uses. Something here did not, and nothing later is going to: the next run makes
its own, and this grows for as long as the machine lives (#417)."

pass "the suite left nothing behind in $tmp"
