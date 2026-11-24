#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/df-missing-column.sh
#
# unix_disk_report() and unix_inode_report() find three columns by the names
# in the report's header: free, capacity and mount point. selectcolumn()
# returns -1 for a name the header does not have, and getcolumn(row, -1)
# returns the row's first column, the device name. atol("/dev/sda1") is 0, so
# a column that was not found was read as 0:
#
#   - free column missing: a rule in units (DISK / 1000U 500U) saw 0 units
#     free and turned every filesystem red, with "(0 units free)"
#   - capacity column missing: every filesystem was 0% used, and the status
#     stayed green whatever the disks held
#
# Only a header with neither the capacity nor the mount column was reported.
# One column missing happens when a client and the server disagree on a name
# (the AIX inode report did, on its free column).
#
# Pinned here, by driving the real xymond_client in --local --no-update mode
# (statuses print to stdout instead of being sent):
#
#   - without the free column, a filesystem under a rule in units is yellow
#     and says the column is missing; it is never given "0 units free"
#   - without the free column, rules in percent work as before and say
#     nothing about it
#   - without the capacity column, the status is yellow and names the column,
#     in the disk and the inode status alike

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin XYMOND_CLIENT xymond/xymond_client

work=$(mktempdir)
mkdir -p "$work/home"
cat > "$work/analysis.cfg" <<'CFG'
HOST=units.test
	DISK * 1000U 500U
	INODE * 1000U 500U
CFG

# status HOST TEST: the one status HOST got for TEST, cut out of $out.
status()
{
	printf '%s\n' "$out" | awk -v want="status $(printf '%s' "$1" | tr . ,).$2 " '
		{ sub(/^[ \t]+/, "") }
		/^status/ { p = (index($0, want) == 1) }
		p'
}

# Three Linux hosts. The handler asks for "Available", "Capacity" and
# "Mounted" in [df], and for "IFree", "IUse%" and "Mounted" in [inode].
#   units.test    no free column, rules in units
#   percent.test  no free column, default rules in percent; /full is full
#   nocapa.test   no capacity column; /full is full
out=$(printf '%s' '@@client|1755640000|127.0.0.1|units.test|linux|linux|
[df]
Filesystem 1024-blocks Used Avail Capacity Mounted on
/dev/sda1 1000000 400000 600000 40% /
[inode]
Filesystem Inodes IUsed Free IUse% Mounted on
/dev/sda1 65536 1024 64512 2% /
@@
@@client|1755640000|127.0.0.1|percent.test|linux|linux|
[df]
Filesystem 1024-blocks Used Avail Capacity Mounted on
/dev/sda1 1000000 400000 600000 40% /
/dev/sdb1 1000000 1000000 0 100% /full
[inode]
Filesystem Inodes IUsed Free IUse% Mounted on
/dev/sda1 65536 1024 64512 2% /
/dev/sdb1 65536 65536 0 100% /full
@@
@@client|1755640000|127.0.0.1|nocapa.test|linux|linux|
[df]
Filesystem 1024-blocks Used Available Use% Mounted on
/dev/sda1 1000000 400000 600000 40% /
/dev/sdb1 1000000 1000000 0 100% /full
[inode]
Filesystem Inodes IUsed IFree Use% Mounted on
/dev/sda1 65536 1024 64512 2% /
/dev/sdb1 65536 65536 0 100% /full
@@
' | env \
	XYMONHOME="$work/home" \
	XYMONTMP="$work" \
	"$XYMOND_CLIENT" --local --no-update --config="$work/analysis.cfg" \
	2>"$work/stderr.log") || fail "xymond_client exited non-zero: $(cat "$work/stderr.log")"

# A rule in units and no free column: said, not read as 0.
for t in disk inode; do
	s=$(status units.test $t)
	[ "$t" = disk ] && col=Available || col=IFree
	assert_not_contains "(0 units free)" "$s" \
		"$t: a missing free column is still read as 0 units free"
	assert_match "^status units,test\.$t yellow " "$s" \
		"$t: a rule in units that cannot be checked did not turn the status yellow"
	assert_contains "/ is checked in units free, and the report has no '$col' column" "$s" \
		"$t: the status does not say which column the rule needed"
done
assert_contains "<!-- ID=/ --> / is checked in units free" "$(status units.test inode)" \
	"inode: the line about the missing column lost its ID comment"

# Rules in percent do not need the free column.
for t in disk inode; do
	s=$(status percent.test $t)
	assert_match "^status percent,test\.$t red " "$s" \
		"$t: a full filesystem no longer alerts when the free column is missing"
	assert_contains "/full (100% used) has reached the PANIC level" "$s" \
		"$t: the full filesystem lost its PANIC line"
	assert_not_contains "is checked in units free" "$s" \
		"$t: a rule in percent complains about the free column, which it does not read"
done

# No capacity column: nothing was checked, so the status may not be green.
for t in disk inode; do
	s=$(status nocapa.test $t)
	[ "$t" = disk ] && col=Capacity || col='IUse%'
	assert_match "^status nocapa,test\.$t yellow " "$s" \
		"$t: without the capacity column the status is not yellow: a full filesystem goes unseen"
	assert_contains "Expected string ($col) not found in df output" "$s" \
		"$t: the status does not name the missing capacity column"
	assert_contains "/dev/sdb1 " "$s" \
		"$t: the report itself is no longer shown"
done

pass "a df column the header does not have is reported, not read as 0"
