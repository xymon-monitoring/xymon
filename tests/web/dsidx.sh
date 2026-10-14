#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/web/dsidx.sh
#
# Compiles and runs dsidx-harness.c, which #includes web/dsidx.inc.c -- the
# source showgraph.c compiles -- and fails if any golden output differs.
# See the harness for what it pins (DSCOUNT expansion, @PREVDSIDX@ and @DSSTART:N@ bounds, verbatim lines, and the DEF-line parsers).

set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
# shellcheck source=tests/lib/assert.sh
. "$here/../lib/assert.sh"

CC=${CC:-cc}
command -v "$CC" >/dev/null 2>&1 || skip "no C compiler available (CC=$CC)"

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

harness="$work/dsidx"
if ! asan_usable || ! "$CC" -g -O1 -fsanitize=address,undefined -iquote "$here/../../web" \
		-o "$harness" "$here/dsidx-harness.c" 2>"$work/cc-asan.log"; then
	"$CC" -g -O1 -iquote "$here/../../web" -o "$harness" "$here/dsidx-harness.c" \
		2>"$work/cc.log" || { cat "$work/cc.log" >&2; fail "harness does not compile"; }
fi

rc=0
ASAN_OPTIONS="detect_leaks=0${ASAN_OPTIONS:+:$ASAN_OPTIONS}" \
	"$harness" >"$work/run.log" 2>&1 || rc=$?
if [ "$rc" != 0 ]; then
	cat "$work/run.log" >&2
	fail "web/dsidx.inc.c does not produce the expected expansions (see output above)"
fi

pass "web/dsidx.inc.c expands every case its harness pins"
