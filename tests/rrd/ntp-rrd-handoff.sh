#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/rrd/ntp-rrd-handoff.sh
#
# Compiles and runs ntp-rrd-handoff-harness.c (the REAL do_net.c and
# do_ntpstat.c against stub RRD plumbing) and fails if it does. See the harness
# for the regression it pins (the do_net.c -> do_ntpstat.c "offset=" handoff and
# its out-of-bounds read at the start of the message). Built with
# AddressSanitizer when the toolchain supports it, so a reintroduced
# out-of-bounds read crashes the run even if the stray byte passes the check.

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

exit 0
