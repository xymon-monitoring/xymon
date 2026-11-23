#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/disk-all-excluded-clear.sh
#
# A client whose XYMONCLIENT_FS_* settings exclude every mounted filesystem
# has no disk report to send, and writes in [df] why:
#
#   Disk report collection failed: every filesystem excluded by the
#   XYMONCLIENT_FS_* settings
#
# unix_disk_report() found no header in that and answered as for a report it
# could not read: yellow, "Expected strings (Capacity and Mounted) not found in
# df output". Nothing to check is what "clear" is for, as for a host with no
# process, log, file or port checks. Pinned here, by driving the real
# xymond_client in --local --no-update mode (statuses print to stdout):
#
#   - that line gives a clear disk status that says why, and is not logged as
#     an incomprehensible report
#   - a DISK rule that requires a filesystem to exist still alerts: the host
#     was told to have it, and the report does not show it
#   - any other header-less report -- a df that failed -- stays yellow
#   - --clear-color applies, as it does to the other "nothing to check" statuses

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin XYMOND_CLIENT xymond/xymond_client

work=$(mktempdir)
mkdir -p "$work/home"
cat > "$work/analysis.cfg" <<'CFG'
HOST=required.test
	DISK /data 90 95 1 -1 red
CFG

EXCLUDED='Disk report collection failed: every filesystem excluded by the XYMONCLIENT_FS_* settings'

# report HOST DF-LINE [xymond_client option]: the statuses for one Linux client
# whose [df] section is that one line.
report() {
	printf '@@client|1755640000|127.0.0.1|%s|linux|linux|\n[df]\n%s\n@@\n' "$1" "$2" | env \
		XYMONHOME="$work/home" \
		XYMONTMP="$work" \
		"$XYMOND_CLIENT" --local --no-update --config="$work/analysis.cfg" ${3:+"$3"} \
		2>"$work/stderr.log" || fail "xymond_client exited non-zero: $(cat "$work/stderr.log")"
}

out=$(report excluded.test "$EXCLUDED")
assert_contains "excluded,test.disk clear " "$out" \
	"a disk report emptied by the client's settings is a clear status"
assert_contains "&clear No filesystems to check: every one is excluded by the client's XYMONCLIENT_FS_* settings" "$out" \
	"the status says why there is nothing to check"
assert_not_contains "Expected strings" "$out" \
	"the client's line is not read as a df output without its header"
assert_not_contains "incomprehensible" "$(cat "$work/stderr.log")" \
	"the report is not logged as incomprehensible"

out=$(report required.test "$EXCLUDED")
assert_contains "required,test.disk red " "$out" \
	"a rule that requires a filesystem still alerts when every one is excluded"
assert_contains "&red Filesystem /data (found 0, req. 1 or more)" "$out" \
	"the missing filesystem is named"

out=$(report failed.test 'Disk report collection failed: df exited 1 with no output')
assert_contains "failed,test.disk yellow " "$out" \
	"a df that failed is still a yellow status"
assert_contains "Expected strings (Capacity and Mounted) not found in df output" "$out" \
	"a df that failed is still reported as a report without its header"

out=$(report excluded.test "$EXCLUDED" --clear-color=green)
assert_contains "excluded,test.disk green " "$out" \
	"--clear-color applies to the disk status with nothing to check"

pass "a disk report with every filesystem excluded by the client's settings is clear, not an unreadable report"
