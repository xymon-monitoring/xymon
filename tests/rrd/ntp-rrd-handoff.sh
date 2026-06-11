#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/rrd/ntp-rrd-handoff.sh
#
# Regression test for the network "ntp" test -> ntpstat RRD chain
# (xymond/rrd/do_net.c -> xymond/rrd/do_ntpstat.c).
#
# do_net_rrd() hands the parsed clock offset to do_ntpstat_rrd() as a
# message that STARTS with the "offset=" token. The PR #158 review found
# that do_ntpstat_rrd() read *(p-1) - one byte before the message buffer -
# to require a separator before the token, an out-of-bounds read that
# (usually) rejected the handoff, so the offset graph silently died. A
# standalone parser harness had missed it: the defect lives in the seam
# between the two files, so this test compiles the REAL do_net.c and
# do_ntpstat.c (included the same way xymond/do_rrd.c does) against stub
# RRD plumbing and drives every documented tool output format through the
# full chain. When the toolchain has AddressSanitizer the harness is built
# with it, so a reintroduced out-of-bounds read crashes the test even if
# the stray byte happens to satisfy the separator check.

set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)

# tests/lib/assert.sh arrives with the tests-bootstrap series (PR #101);
# source it when present, otherwise provide the minimal vocabulary inline
# so the test also runs standalone on a tree without the suite.
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

harness="$work/ntp-rrd-handoff"
if ! "$CC" -g -O1 -fsanitize=address,undefined -o "$harness" \
		"$here/ntp-rrd-handoff-harness.c" 2>"$work/cc-asan.log"; then
	# No sanitizer support in this toolchain - the functional assertions
	# still catch the regression deterministically.
	"$CC" -g -O1 -o "$harness" "$here/ntp-rrd-handoff-harness.c" 2>"$work/cc.log" \
		|| { cat "$work/cc.log" >&2; fail "harness does not compile"; }
fi

# The regression is an out-of-bounds READ - ASan's job. LeakSanitizer adds
# nothing here and its ptrace-based final pass is blocked in some sandboxes
# (WSL, unprivileged containers), failing the run after all cases passed.
#
# Stay quiet on success like the rest of the suite: the harness prints a
# per-case progress line for every format, useful diagnostics on a failure
# but noise on a pass. Capture it (stdout plus any ASan/UBSan report on
# stderr) and replay it only when the harness fails.
if ! ASAN_OPTIONS="detect_leaks=0${ASAN_OPTIONS:+:$ASAN_OPTIONS}" \
		"$harness" >"$work/run.log" 2>&1; then
	cat "$work/run.log" >&2
	fail "ntp tool output -> ntpstat RRD handoff is broken (see output above)"
fi

pass "all NTP tool formats reach the ntpstat RRD through the do_net.c -> do_ntpstat.c handoff"
