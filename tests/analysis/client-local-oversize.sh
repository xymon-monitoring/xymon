#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/analysis/client-local-oversize.sh
#
# In local mode xymonclient.sh pipes the client data file into
# "xymond_client --local". get_xymond_message() read it with the C_CLIENT
# channel's limit (MAXMSG_CLIENT, 512 KB by default) although no channel is
# involved, so a larger report was cut: "Got over-size message, truncating",
# and every section after the cut was lost -- typically [ps] and [ports],
# with the procs, ports and cpu checks (#681).
#
# Driven here with MAXMSG_CLIENT=64 (KB) and a report of about 200 KB: a
# large [ps] section, then [df] last. Checked:
#
#   - the whole report is read: no over-size error, and the [df] section
#     after the cut point still yields its disk status
#   - the message after it on the same stream is read too, so the grown
#     buffer keeps the stream in sync

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin XYMOND_CLIENT xymond/xymond_client

work=$(mktempdir)
mkdir -p "$work/home"
: > "$work/analysis.cfg"

{
	printf '%s\n' '@@client|1755640000|127.0.0.1|big.test|linux|linux|' '[ps]' \
		'PID PPID USER STARTED S PRI %CPU TIME %MEM RSZ VSZ CMD'
	i=0
	while [ $i -lt 2500 ]; do
		printf '%d 1 root 10:00 S 20 0.0 00:00:00 0.0 1000 2000 /usr/sbin/daemon --instance %d --padding xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\n' $((i + 100)) $i
		i=$((i + 1))
	done
	printf '%s\n' '[df]' 'Filesystem 1024-blocks Used Available Capacity Mounted on' \
		'/dev/sdb1 1000000 1000000 0 100% /full' '@@'
	printf '%s\n' '@@client|1755640000|127.0.0.1|small.test|linux|linux|' '[df]' \
		'Filesystem 1024-blocks Used Available Capacity Mounted on' \
		'/dev/sda1 1000000 400000 600000 40% /' '@@'
} > "$work/msgs"

size=$(wc -c < "$work/msgs")
[ "$size" -gt $((3 * 64 * 1024)) ] || fail "test data is only $size bytes, not over three times the 64 KB limit"

out=$(env \
	XYMONHOME="$work/home" \
	XYMONTMP="$work" \
	MAXMSG_CLIENT=64 \
	"$XYMOND_CLIENT" --local --no-update --config="$work/analysis.cfg" \
	<"$work/msgs" 2>"$work/stderr.log") || fail "xymond_client exited non-zero: $(cat "$work/stderr.log")"

assert_not_contains "over-size" "$(cat "$work/stderr.log")" \
	"xymond_client --local truncated the report at MAXMSG_CLIENT"
assert_contains "&red /full (100% used) has reached the PANIC level" "$out" \
	"the [df] section after the 64 KB mark was lost"
assert_match "small,test\.disk green" "$out" \
	"the message after the large one was not read"

pass "xymond_client --local reads a client report larger than MAXMSG_CLIENT whole"
