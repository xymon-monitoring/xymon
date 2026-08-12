#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/buildsystem/testsuite-stdin-isolation.sh
#
# The runner walks its list of tests with `while read t ... done <<EOF`, so the
# list lives on its own stdin. A test that reads stdin therefore eats what is
# left of it: the loop ends at that test, and the runner prints a summary with
# no failures and exits 0 for the tests it never ran. Found the hard way -- a
# test invoking rrdcachectl, which reads hostnames until end of input, silently
# cut a 75-test run down to 51.
#
# That is the one failure a test runner must not have, so it is checked here on
# a fixture: a real copy of the runner, one test that drains stdin, and one
# after it that must still be reached.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
work=$(mktempdir)

# The runner resolves its root from its own location, so a copy in a fixture
# tree discovers that tree's tests and nothing else.
mkdir -p "$work/tests"
cp "$ROOT/tests/testsuite" "$work/tests/testsuite"
chmod +x "$work/tests/testsuite"

cat >"$work/tests/aaa-drains-stdin.sh" <<'EOF'
#!/bin/sh
cat >/dev/null
exit 0
EOF

cat >"$work/tests/zzz-marker.sh" <<EOF
#!/bin/sh
: > "$work/reached"
exit 0
EOF

chmod +x "$work/tests/aaa-drains-stdin.sh" "$work/tests/zzz-marker.sh"

out=$("$work/tests/testsuite" 2>&1) || fail "the fixture run failed: $out"

[ -f "$work/reached" ] \
	|| fail "the test after the stdin-draining one never ran, and the run still reported success: $out"
assert_contains "passed:  2" "$out" \
	"the runner counted fewer tests than it has, without failing"

pass "a test that reads stdin cannot truncate the run"
