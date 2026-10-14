#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/xymond/hostdata-minfree-validate.sh
#
# --minimum-free must go through the validated option parser: with atoi,
# garbage collapsed to 0 and silently disabled the free-space protection,
# and out-of-range values passed through unchecked. Disk fullness cannot
# be arranged in a test, so this asserts the parser's verdicts on stderr:
# garbage falls back to the default of 5, negatives clamp to 0 (check
# off, as with atoi), values above 100 are capped.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
# shellcheck source=tests/lib/xymond-hostdata.sh
. "$(dirname "$0")/../lib/xymond-hostdata.sh"

work=$(mktempdir)

setup_xymond_hostdata "$work"

one_msg() { printf '@@clichg#1|1|10.0.0.99|realhost|F1|linux\npayload\n@@\n'; }

one_msg | run_xymond_hostdata "$work" --minimum-free=junk \
	>/dev/null 2>"$work/minfree.log" || true
grep -q -- "--minimum-free=junk is not a plain integer, using default 5" "$work/minfree.log" \
	|| { cat "$work/minfree.log" >&2; fail "--minimum-free=junk was not rejected for the default of 5"; }

# A numeric value with a trailing unit is a plain-integer violation, not a
# valid number: "10%" (a natural but wrong spelling, since the value is
# already a percentage) is rejected, not silently parsed as 10. Catches the
# wrong-unit typo instead of quietly honoring half of it.
one_msg | run_xymond_hostdata "$work" --minimum-free=10% \
	>/dev/null 2>"$work/minfree.log" || true
grep -q -- "--minimum-free=10% is not a plain integer, using default 5" "$work/minfree.log" \
	|| { cat "$work/minfree.log" >&2; fail "--minimum-free=10% was not rejected as a non-integer"; }

one_msg | run_xymond_hostdata "$work" --minimum-free=-5 \
	>/dev/null 2>"$work/minfree.log" || true
grep -q -- "--minimum-free=-5 is below 0, using 0" "$work/minfree.log" \
	|| { cat "$work/minfree.log" >&2; fail "--minimum-free=-5 was not clamped to 0"; }

# A plain integer above the range is clamped, not rejected: 200 is a valid
# number, just out of bounds, so it caps at 100 rather than reverting to
# the default.
one_msg | run_xymond_hostdata "$work" --minimum-free=200 \
	>/dev/null 2>"$work/minfree.log" || true
grep -q -- "--minimum-free=200 is too large, capping at 100" "$work/minfree.log" \
	|| { cat "$work/minfree.log" >&2; fail "--minimum-free=200 was not capped at 100"; }

pass "a garbage --minimum-free is refused rather than collapsing to 0 and disabling the free-space guard"
