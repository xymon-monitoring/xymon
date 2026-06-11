#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/network/ntp-cmd.sh
#
# Compiles and runs ntp-cmd-harness.c (the real xymonnet/ntpcmd.c against a stub
# errprintf) and fails if it does. See the harness for the regression it pins
# (the per-tool timeout flag -- -u for sntp, -t for ntpdig/chronyd -- and the
# selection/forcing/fallback rules).

set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)

if [ -r "$here/../lib/assert.sh" ]; then
	# shellcheck source=tests/lib/assert.sh
	. "$here/../lib/assert.sh"
else
	fail() { printf 'FAIL: %s\n' "$*" >&2; exit 1; }
	skip() { printf 'SKIP: %s\n' "$*" >&2; exit 77; }
fi

CC=${CC:-cc}
command -v "$CC" >/dev/null 2>&1 || skip "no C compiler available (CC=$CC)"

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

harness="$work/ntp-cmd"
if ! "$CC" -g -O1 -fsanitize=address,undefined -o "$harness" \
		"$here/ntp-cmd-harness.c" 2>"$work/cc-asan.log"; then
	"$CC" -g -O1 -o "$harness" "$here/ntp-cmd-harness.c" 2>"$work/cc.log" \
		|| { cat "$work/cc.log" >&2; fail "harness does not compile"; }
fi

if ! ASAN_OPTIONS="detect_leaks=0${ASAN_OPTIONS:+:$ASAN_OPTIONS}" \
		"$harness" >"$work/run.log" 2>&1; then
	cat "$work/run.log" >&2
	fail "NTP tool selection / command construction is broken (see output above)"
fi

exit 0
