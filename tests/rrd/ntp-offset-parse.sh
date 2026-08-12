#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/rrd/ntp-offset-parse.sh
#
# Compiles and runs ntp-offset-parse-harness.c, which drives the real do_net.c +
# do_ntpstat.c offset parsing for the "ntp" test across both backends
# (built-in probe banner, ntpdate) and the do_ntpstat "offset=" path, with
# the RRD plumbing stubbed. Fails if any offset is parsed or scaled wrongly.

set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)

if [ -r "$here/../lib/assert.sh" ]; then
	# shellcheck source=tests/lib/assert.sh
	. "$here/../lib/assert.sh"
else
	fail() { printf 'FAIL: %s\n' "$*" >&2; exit 1; }
	skip() { printf 'SKIP: %s\n' "$*" >&2; exit 77; }
	require_cc() { CC=${CC:-cc}; command -v "$CC" >/dev/null 2>&1 || skip "no C compiler available (CC=$CC)"; }
	# Standalone copy, no assert.sh to probe with: take the plain build.
	asan_usable() { return 1; }
fi

require_cc

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# The sanitized build is a bonus, so fall back to a plain one whenever it is
# not available -- including where -fsanitize links but will not run (see
# asan_usable), which no compile check can see.
harness="$work/ntp-offset-parse"
if ! asan_usable || ! "$CC" -g -O1 -fsanitize=address,undefined -o "$harness" \
		"$here/ntp-offset-parse-harness.c" 2>"$work/cc-asan.log"; then
	"$CC" -g -O1 -o "$harness" "$here/ntp-offset-parse-harness.c" 2>"$work/cc.log" \
		|| { cat "$work/cc.log" >&2; fail "harness does not compile"; }
fi

if ! ASAN_OPTIONS="detect_leaks=0${ASAN_OPTIONS:+:$ASAN_OPTIONS}" \
		"$harness" >"$work/run.log" 2>&1; then
	cat "$work/run.log" >&2
	fail "ntp offset parsing/scaling is broken (see output above)"
fi

exit 0
