#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/buildsystem/testsuite-guards.sh
#
# Behavioural guard for the runner's own guards: the label/tree witness, the
# coverage floor, the strict filing checks, and the strict switch itself.
# Each was proven by planting a trap by hand when it was written; this test
# commits those traps, so breaking a guard turns a lane red.
#
# We copy tests/testsuite into a temp root with a planted mini-suite and run
# it there -- the inner runner resolves its own root from $0, so it never
# touches the real tree. Each scenario controls its environment explicitly;
# GITHUB_ACTIONS is cleared so the inner output is the plain format the
# assertions match. Patterns are anchored to the runner's exact message
# prefixes: a refusal for the wrong reason must not satisfy a scenario.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)

work=$(mktempdir)
mkdir -p "$work/tests"
cp "$ROOT/tests/testsuite" "$work/tests/testsuite"
chmod 755 "$work/tests/testsuite"

plant() { # plant PATH BODY -- an executable planted test
	mkdir -p "$work/$(dirname "$1")"
	printf '#!/bin/sh\n%s\n' "$2" >"$work/$1"
	chmod 755 "$work/$1"
}

# Baseline passes in three areas -- universal, client, server -- so a variant
# row silently losing an area cannot hide behind a one-area baseline.
plant tests/common/baseline-common.sh 'echo PASS: baseline; exit 0'
plant tests/client/baseline-client.sh 'echo PASS: baseline; exit 0'
plant tests/server/baseline-server.sh 'echo PASS: baseline; exit 0'

inner_out=''
inner_rc=0
run_inner() { # run_inner [VAR=VAL ...] -- run the planted suite
	inner_rc=0
	inner_out=$(cd "$work" && env -u XYMON_TESTS_PARTIAL_LOG \
		GITHUB_ACTIONS= XYMON_TESTS_STRICT= XYMON_VARIANT= "$@" \
		./tests/testsuite 2>&1) || inner_rc=$?
}
expect() { # expect RC PATTERN LABEL -- PATTERN is a grep BRE, ^-anchorable
	[ "$inner_rc" = "$1" ] \
		|| fail "$3: expected rc=$1, got rc=$inner_rc -- output: $inner_out"
	printf '%s\n' "$inner_out" | grep -q "$2" \
		|| fail "$3: output lacks '$2' -- output: $inner_out"
}

# (1) baseline: a labeled strict server run sees all three areas pass.
run_inner XYMON_TESTS_STRICT=1 XYMON_VARIANT=server
expect 0 '^passed:  3$' 'strict baseline'

# (2) the floor: a skip in a provided area fails a strict run, naming the
# test; without strict the same skip stays a skip.
plant tests/server/planted-skip.sh 'echo SKIP: planted; exit 77'
run_inner XYMON_TESTS_STRICT=1 XYMON_VARIANT=server
expect 1 '^FAIL: coverage floor:' 'floor breach'
expect 1 'tests/server/planted-skip\.sh' 'floor names the skipped test'
run_inner XYMON_VARIANT=server
expect 0 '^skipped: 1$' 'floor needs strict'
rm "$work/tests/server/planted-skip.sh"

# (3) strict filing: an unknown area refuses the run even for a PASSING test
# -- the filing error is the offence, not the outcome. Without strict it runs
# with a notice.
plant tests/newarea/planted-pass.sh 'echo PASS: planted; exit 0'
run_inner XYMON_TESTS_STRICT=1 XYMON_VARIANT=server
expect 1 '^FAIL: unknown test area' 'unknown area refused under strict'
run_inner XYMON_VARIANT=server
expect 0 '^NOTE: unknown test area' 'unknown area still runs without strict'
expect 0 '^passed:  4$' 'unknown area counted without strict'
rm -r "$work/tests/newarea"

# (4) strict filing: a passing test directly under tests/ refuses the run.
plant tests/planted-toplevel.sh 'echo PASS: planted; exit 0'
run_inner XYMON_TESTS_STRICT=1 XYMON_VARIANT=server
expect 1 '^FAIL: top-level test' 'top-level test refused under strict'
rm "$work/tests/planted-toplevel.sh"

# (5) strict filing: an executable .sh in either helper directory refuses the
# run and names it -- two directories, two different names, so a guard
# narrowed to one path or one filename goes red.
plant tests/lib/planted-stray.sh 'echo FAIL: invisible; exit 1'
run_inner XYMON_TESTS_STRICT=1 XYMON_VARIANT=server
expect 1 '^FAIL: executable \.sh under tests/lib/' 'stray in tests/lib/ refused'
expect 1 'planted-stray\.sh' 'stray in tests/lib/ named'
rm -r "$work/tests/lib"
plant tests/fixtures/extra-helper.sh 'echo FAIL: invisible; exit 1'
run_inner XYMON_TESTS_STRICT=1 XYMON_VARIANT=server
expect 1 '^FAIL: executable \.sh under tests/fixtures/' 'stray in tests/fixtures/ refused'
rm -r "$work/tests/fixtures"

# (6) the witness: a label disagreeing with the configured tree refuses the
# run; a matching label runs with the server-only area filtered. Both client
# trees, because the original mislabel was localclient-vs-client.
printf 'CLIENTONLY = yes\nLOCALCLIENT = no\n' >"$work/Makefile"
run_inner XYMON_VARIANT=server
expect 1 'but this tree is configured client$' 'client tree vs server label refused'
run_inner XYMON_VARIANT=client
expect 0 '^passed:  2$' 'matching client label runs'
printf 'CLIENTONLY = yes\nLOCALCLIENT = yes\n' >"$work/Makefile"
run_inner XYMON_VARIANT=client
expect 1 'but this tree is configured localclient$' 'localclient tree vs client label refused'
run_inner XYMON_VARIANT=localclient
expect 0 '^passed:  2$' 'matching localclient label runs'
rm "$work/Makefile"

# (7) the strict switch: a value it does not know is an error, not a silent
# off; strict without a variant cannot be held to a per-variant floor.
run_inner XYMON_TESTS_STRICT=true XYMON_VARIANT=server
expect 1 'XYMON_TESTS_STRICT' 'unknown strict value refused'
run_inner XYMON_TESTS_STRICT=1
expect 1 'needs XYMON_VARIANT' 'strict without a variant refused'

pass "the runner's guards hold: floor, strict filing, strict switch, label/tree witness"
