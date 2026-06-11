#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/network/ntp-cmd.sh
#
# Regression test for xymonnet NTP tool selection and command construction
# (xymonnet/ntpcmd.c). The PR #158 review found that forced NTPTOOL=sntp (and
# the legacy SNTP / classic-sntp fallback) emitted "sntp -t N host": classic
# ntp.org sntp 4.2.8 has no -t and rejects it; its unicast-response timeout is
# -u. The RRD-handoff harness only parses tool *output*, so it could not catch
# a malformed command line. This compiles the real ntpcmd.c against a stub
# errprintf and asserts the per-tool timeout flag (-u for sntp, -t for ntpdig
# and chronyd) plus the priority/forcing/fallback rules.

set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)

if [ -r "$here/../lib/assert.sh" ]; then
	# shellcheck source=tests/lib/assert.sh
	. "$here/../lib/assert.sh"
else
	fail() { printf 'FAIL: %s\n' "$*" >&2; exit 1; }
	skip() { printf 'SKIP: %s\n' "$*" >&2; exit 77; }
	pass() { printf 'PASS: %s\n' "${*:-ok}"; exit 0; }
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

pass "NTP tool selection and per-tool timeout flags are correct"
