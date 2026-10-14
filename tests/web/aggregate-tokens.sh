#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/web/aggregate-tokens.sh
#
# Compiles and runs aggregate-tokens-harness.c, which #includes web/aggregate-tokens.inc.c -- the
# source showgraph.c compiles -- and fails if any golden output differs.
# See the harness for what it pins (the RPN for each aggregate operator, NaN-aware variants, percentiles, empty sets and @DSMEDIAN:).

set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
# shellcheck source=tests/lib/assert.sh
. "$here/../lib/assert.sh"

CC=${CC:-cc}
command -v "$CC" >/dev/null 2>&1 || skip "no C compiler available (CC=$CC)"

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

harness="$work/aggregate-tokens"
if ! asan_usable || ! "$CC" -g -O1 -fsanitize=address,undefined -iquote "$here/../../web" \
		-o "$harness" "$here/aggregate-tokens-harness.c" 2>"$work/cc-asan.log"; then
	"$CC" -g -O1 -iquote "$here/../../web" -o "$harness" "$here/aggregate-tokens-harness.c" \
		2>"$work/cc.log" || { cat "$work/cc.log" >&2; fail "harness does not compile"; }
fi

rc=0
ASAN_OPTIONS="detect_leaks=0${ASAN_OPTIONS:+:$ASAN_OPTIONS}" \
	"$harness" >"$work/run.log" 2>&1 || rc=$?
if [ "$rc" != 0 ]; then
	cat "$work/run.log" >&2
	fail "web/aggregate-tokens.inc.c does not produce the expected expansions (see output above)"
fi

pass "web/aggregate-tokens.inc.c expands every case its harness pins"
