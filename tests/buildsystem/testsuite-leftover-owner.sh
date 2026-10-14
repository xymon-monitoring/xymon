#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# no-leftovers.sh names the test that left something in the run directory.
#
# A leftover that appears once in many runs -- a FIFO bash leaves for a
# process substitution, say -- cannot be traced by running the suite again, so
# the run that sees it has to say where it came from. The runner records what
# is new in the run directory after each test, and no-leftovers.sh puts that
# test beside each path.
#
# Against a fake tree with the real runner, tests/lib and no-leftovers.sh: a
# clean test, one that leaves a file, and another clean one after it. The file
# must be put on the second, and neither of the others named -- the third
# would be, if a leftover were charged to whichever test ran last.
set -euo pipefail
. "$(dirname "$0")/../lib/assert.sh"
ROOT=$(find_root)

work=$(mktempdir); register_cleanup "rm -rf '$work'"

# A throwaway tree with a dummy area: the calling run's variant and coverage
# floor are not its to meet (see testsuite-run-disposal.sh).
unset XYMON_TESTS_STRICT XYMON_VARIANT

tree=$work/tree
mkdir -p "$tree/tests/dummy" "$tree/tests/final"
cp "$ROOT/tests/testsuite" "$tree/tests/testsuite"
cp -R "$ROOT/tests/lib" "$tree/tests/lib"
cp "$ROOT/tests/final/no-leftovers.sh" "$tree/tests/final/no-leftovers.sh"
printf '#!/bin/sh\nexit 0\n' >"$tree/tests/dummy/a-clean.sh"
printf '#!/bin/sh\n: >"$TMPDIR/left-behind"\nexit 0\n' >"$tree/tests/dummy/b-leaks.sh"
printf '#!/bin/sh\nexit 0\n' >"$tree/tests/dummy/c-clean.sh"
chmod +x "$tree/tests/testsuite" "$tree/tests/final/no-leftovers.sh" "$tree"/tests/dummy/*.sh

rc=0
out=$(cd "$tree" && ./tests/testsuite 2>&1) || rc=$?

# Non-vacuity: the leftover has to be seen at all, or the naming below is
# checked on a message that was never printed.
[ "$rc" -ne 0 ] || fail "the fake suite left a file in its run directory and still passed:
$out"
assert_contains "left-behind" "$out" "no-leftovers.sh did not report the file the fake test left"

assert_contains "left-behind  (left by tests/dummy/b-leaks.sh)" "$out" \
	"the leftover was not put on the test that left it"
case $out in
	*"left by tests/dummy/a-clean.sh"*|*"left by tests/dummy/c-clean.sh"*)
		fail "a test that left nothing was named as the owner of a leftover:
$out" ;;
esac

pass "no-leftovers.sh names the test that left each leftover, not the one before or after it"
