#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/inode-empty-clear.sh
#
# A client sends an empty [inode] section when none of its filesystems counts
# inodes (a host with only ZFS, or only APFS), or when its XYMONCLIENT_FS_*
# settings leave every one out. unix_inode_report() answered green, "No
# filesystems reporting inode data": a check that passed, where none was
# made. Nothing to check is what "clear" is for, as for the disk status of a
# client with every filesystem excluded (disk-all-excluded-clear.sh). Pinned
# here, by driving the real xymond_client in --local --no-update mode
# (statuses print to stdout):
#
#   - an empty [inode] gives a clear inode status that says why
#   - an INODE rule that requires a filesystem to exist still alerts
#   - an [inode] with a filesystem in it is still green
#   - a df that failed, which the client reports in one line, stays yellow
#   - --clear-color applies, as it does to the other "nothing to check" statuses

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin XYMOND_CLIENT xymond/xymond_client

work=$(mktempdir)
mkdir -p "$work/home"
cat > "$work/analysis.cfg" <<'CFG'
HOST=required.test
	INODE /data 70 90 1 -1 red
CFG

# report HOST INODE-SECTION [xymond_client option]: the statuses for one Linux
# client whose [inode] section is that text, which may be empty.
report() {
	printf '@@client|1755640000|127.0.0.1|%s|linux|linux|\n[inode]\n%s@@\n' "$1" "${2:+$2
}" | env \
		XYMONHOME="$work/home" \
		XYMONTMP="$work" \
		"$XYMOND_CLIENT" --local --no-update --config="$work/analysis.cfg" ${3:+"$3"} \
		2>"$work/stderr.log" || fail "xymond_client exited non-zero: $(cat "$work/stderr.log")"
}

out=$(report empty.test "")
assert_contains "empty,test.inode clear " "$out" \
	"an empty inode report is a clear status"
assert_contains "&clear No filesystems reporting inode data" "$out" \
	"the status says why there is nothing to check"
assert_not_contains "incomprehensible" "$(cat "$work/stderr.log")" \
	"an empty inode report is not logged as incomprehensible"

out=$(report required.test "")
assert_contains "required,test.inode red " "$out" \
	"a rule that requires a filesystem still alerts on an empty inode report"
assert_contains "&red <!-- ID=/data -->Filesystem /data (found 0, req. 1 or more)" "$out" \
	"the missing filesystem is named"

out=$(report measured.test 'Filesystem Inodes IUsed IFree IUse% Mounted on
/dev/sda1 1000 100 900 10% /')
assert_contains "measured,test.inode green " "$out" \
	"an inode report with a filesystem under its thresholds is still green"

out=$(report failed.test 'Inode report collection failed: df exited 1 with no output')
assert_contains "failed,test.inode yellow " "$out" \
	"a df that failed is still a yellow status"

out=$(report empty.test "" --clear-color=green)
assert_contains "empty,test.inode green " "$out" \
	"--clear-color applies to the inode status with nothing to check"

pass "an empty inode report is clear, not green; a required filesystem, a measured one and a failed df read as before"
