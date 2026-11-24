#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/aix-inode-free-column.sh
#
# The AIX client writes the free-inode column under two names. With the
# System V df it copies that df's own word, "ifree"; without it, it reshapes
# df -v and writes "avail":
#
#   Filesystem   itotal   iused   ifree   %iused Mounted on    /usr/sysv/bin/df -i
#   Filesystem   itotal   iused   avail   %iused Mounted on    df -v
#
# aix.c asked for "avail" only. selectcolumn() then returns -1 for the first
# header, and getcolumn(row, -1) returns the row's first column, the device
# name: atol("/dev/hd4") is 0. Percentage rules read "%iused" and were right,
# but an absolute rule (INODE / 1000U 500U) saw 0 inodes free on every
# filesystem and went red, on AIX 6.1 to 7.3 with the System V df.
#
# Pinned here, by driving the real xymond_client in --local --no-update mode
# (statuses print to stdout instead of being sent):
#
#   - under an absolute rule, a filesystem with plenty of free inodes is
#     green with either header, and one below the rule is red with its real
#     count, not 0
#   - the client's unavailable-mount marker ("-" in the free column, 100%)
#     is seen with either header

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin XYMOND_CLIENT xymond/xymond_client

work=$(mktempdir)
mkdir -p "$work/home"
cat > "$work/analysis.cfg" <<'CFG'
DEFAULT
	INODE * 1000U 500U
CFG

# report HOST FREEHDR: one AIX client message whose [inode] section names the
# free column FREEHDR. / has room, /small is under the panic level, /remote is
# the unavailable-mount marker.
report()
{
	printf '%s' "@@client|1755640000|127.0.0.1|$1|aix|aix|
[inode]
Filesystem itotal iused $2 %iused Mounted on
/dev/hd4 65536 1024 64512 2% /
/dev/hd9 400 100 300 25% /small
srv:/exp - - - 100% /remote
@@
" | env \
		XYMONHOME="$work/home" \
		XYMONTMP="$work" \
		"$XYMOND_CLIENT" --local --no-update --config="$work/analysis.cfg" \
		2>"$work/stderr.log" || fail "xymond_client exited non-zero: $(cat "$work/stderr.log")"
}

for hdr in ifree avail; do
	out=$(report "aix.$hdr" "$hdr")

	assert_not_contains "ID=/ --> / (0 units free)" "$out" \
		"with the header '$hdr', / is reported with 0 inodes free: the free column was not found"
	assert_not_contains "ID=/ -->" "$out" \
		"with the header '$hdr', / alerts although it has 64512 inodes free"
	assert_contains "&red <!-- ID=/small --> /small (300 units free) has reached the PANIC level (500 units)" "$out" \
		"with the header '$hdr', /small is not reported with its real free count"
	assert_contains "&red <!-- ID=/remote --> /remote is unreachable (not measured)" "$out" \
		"with the header '$hdr', the unavailable-mount marker was not recognised"
done

pass "xymond reads the AIX inode report's free column under both of the client's names for it"
