#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/web/aggregate-tokens-unit.sh
#
# Golden-output unit tests for the showgraph aggregate tokens
# (@AVG:/@SUM:/@MEDIAN:/@Ppct:/...) and the @DSIDX@ parse-time
# expansion. Both binaries are built and run by web/Makefile's
# "check" target so the parser source is shared with showgraph.c.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)

command -v make >/dev/null 2>&1 || skip "make not available"
[ -f "$ROOT/include/config.h" ] \
	|| skip "tree not configured (run configure/make first; the post-build CI suite covers this)"

work=$(mktemp -d "${TMPDIR:-/tmp}/xymon-aggcheck.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM

[ -w "$ROOT/web" ] || skip "source tree not writable (cannot rebuild the checkers)"
make -C "$ROOT/web" check >"$work/build.log" 2>&1 \
	|| { tail -20 "$work/build.log" >&2; fail "aggregate-token unit tests failed"; }

pass "aggregate-token and @DSIDX@ unit tests pass"
