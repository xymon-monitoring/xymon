#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/analysis/zvse-getvis-colour.sh
#
# The z/VSE getvis report sets its status colour from each partition's
# 24-bit and "any" utilisation. A figure at the warning level set the colour
# to yellow whatever it already was, so a critical figure earlier in the
# report -- in an earlier partition, or the 24-bit figure of the same one --
# was turned back to yellow, while the "&red ... is CRITICAL" line stayed in
# the text.
#
# This drives xymond_client --local --no-update (statuses go to stdout) with
# the default thresholds (warning above 90%, critical above 95%) and checks
# the colour of the getvis status:
#
#   - a critical partition, then one at the warning level: red;
#   - one partition critical on 24-bit and at the warning level on "any": red;
#   - a partition at the warning level only: yellow, so the warning level
#     itself still raises the colour.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin XYMOND_CLIENT xymond/xymond_client

work=$(mktempdir)
mkdir -p "$work/home"
printf 'DEFAULT\n' > "$work/analysis.cfg"

# getvis HOST ROWS: the getvis status line xymond_client emits for ROWS
# (partition, used24, free24, usedany, freeany; one per line).
getvis() {
	printf '@@client|1755640000|127.0.0.1|%s|zvse|zvse|\n[date]\nWed Nov 25 07:00:00 UTC 2026\n[getvis]\nPID Used24 Free24 UsedAny FreeAny\n%s\n@@\n' \
		"$1" "$2" | env \
		XYMONHOME="$work/home" \
		XYMONTMP="$work" \
		"$XYMOND_CLIENT" --local --no-update --config="$work/analysis.cfg" \
		2>"$work/stderr.log" \
		|| fail "$1: xymond_client exited non-zero: $(tail -n 3 "$work/stderr.log")"
}

out=$(getvis "twoparts" "F1 100 0 100 0
F3 92 8 10 90")
assert_contains "status twoparts.getvis red" "$out" \
	"a partition at the warning level after a critical one turned the status from red"
assert_contains "&red 24-bit Getvis utilization for F1 is CRITICAL" "$out" \
	"the critical partition is not reported"

out=$(getvis "onepart" "F1 100 0 92 8")
assert_contains "status onepart.getvis red" "$out" \
	"an \"any\" figure at the warning level turned a critical 24-bit figure's status from red"

out=$(getvis "warnonly" "F3 92 8 10 90")
assert_contains "status warnonly.getvis yellow" "$out" \
	"a partition at the warning level did not make the status yellow"

pass "a z/VSE getvis figure at the warning level does not lower the colour a critical one set"
