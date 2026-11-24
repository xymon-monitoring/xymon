#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/hpux-inode-capacity-column.sh
#
# The HP-UX client heads the inode report's percentage column "iused%"
# (client/xymonclient-hp-ux.sh):
#
#   Filesystem   itotal   ifree   iused   iused% Mounted on
#
# hpux.c asked for "%iused", the other clients' word. selectcolumn() returned
# -1, and getcolumn(row, -1) returns the row's first column, the device name:
# atol("/dev/vg00/lvol3") is 0. So every filesystem was 0% used, and the inode
# status of an HP-UX host stayed green with no inode left.
#
# Pinned here, by driving the real xymond_client in --local --no-update mode
# (statuses print to stdout instead of being sent): with the client's own
# header a filesystem at 100% is red, and one at 2% is not named.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin XYMOND_CLIENT xymond/xymond_client

work=$(mktempdir)
mkdir -p "$work/home"
: > "$work/analysis.cfg"

out=$(printf '%s' '@@client|1755640000|127.0.0.1|hpux.test|hpux|hpux|
[inode]
Filesystem                              itotal      ifree      iused     iused% Mounted on
/dev/vg00/lvol3                          65536      64512       1024          2 /
/dev/vg00/lvol5                           1000          0       1000        100 /full
@@
' | env \
	XYMONHOME="$work/home" \
	XYMONTMP="$work" \
	"$XYMOND_CLIENT" --local --no-update --config="$work/analysis.cfg" \
	2>"$work/stderr.log") || fail "xymond_client exited non-zero: $(cat "$work/stderr.log")"

assert_match "hpux,test\.inode red" "$out" \
	"a filesystem with no inode left does not turn the HP-UX inode status red: the percentage column was not found"
assert_contains "&red <!-- ID=/full --> /full (100% used) has reached the PANIC level" "$out" \
	"the full filesystem is not named with its 100%"
assert_not_contains "ID=/ -->" "$out" \
	"the filesystem at 2% alerts"

pass "xymond reads the HP-UX inode report's percentage column under the client's name for it"
