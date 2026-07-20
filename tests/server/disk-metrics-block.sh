#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/disk-metrics-block.sh
#
# unix_disk_report() synthesizes a self-describing METRICS block into the
# disk status (disk plan, phase 4): DS line identical to do_disk's params,
# one instance line per shown filesystem with do_disk's values (pct from
# the capacity column, used from df column 2), RRDDISKS/NORRDDISKS
# replicated at synthesis, and the linecount hint kept for the legacy
# render path. Drives the real xymond_client with --no-update, which
# prints the generated status messages to stdout.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin XYMOND_CLIENT "xymond/xymond_client"

work=$(mktempdir)
mkdir -p "$work/etc" "$work/tmp"
cat >"$work/etc/hosts.cfg" <<'EOF'
0.0.0.0 testhost # linux
EOF
: >"$work/etc/analysis.cfg"

feed_client() {  # feed_client [ENV=val...] -- one linux client message on stdin
	local ts; ts=$(date +%s)
	{
		printf '@@client|%s|127.0.0.1|testhost|linux|linux||\n' "$ts"
		printf '[df]\n'
		printf 'Filesystem 1024-blocks Used Available Capacity Mounted on\n'
		printf '/dev/sda1 1000 500 500 50%% /var\n'
		printf '/dev/sdb1 1000 900 100 90%% /data\n'
		printf '[inode]\n'
		printf 'Filesystem Inodes IUsed IFree IUse%% Mounted on\n'
		printf '/dev/sda1 65536 6553 58983 10%% /var\n'
		printf '@@\n'
	} | env XYMONHOME="$work" XYMONTMP="$work/tmp" \
		HOSTSCFG="!$work/etc/hosts.cfg" MACHINE=testhost "$@" \
		"$XYMOND_CLIENT" --no-update 2>/dev/null
}

out=$(feed_client)
printf '%s\n' "$out" >"$work/status.out"

assert_contains "testhost.disk" "$out" "a disk status is generated"
assert_contains "<!--XYMON METRICS: disk" "$out" "the disk status carries a METRICS block"
assert_contains "DS:pct:GAUGE:600:0:100 DS:used:GAUGE:600:0:U" "$out" "DS line matches do_disk's schema exactly"
assert_contains "/var 50:500" "$out" "instance line carries do_disk's values (pct:used)"
assert_contains "/data 90:900" "$out" "every shown filesystem gets an instance line"
assert_contains "<!-- linecount=2 -->" "$out" "the linecount hint is kept for the legacy render path"
assert_contains "<!--XYMON METRICS: inode" "$out" "the inode status carries its METRICS block too"
assert_contains "/var 10:6553" "$out" "inode instance line carries pct:iused"

# NORRDDISKS drops a filesystem from STORAGE only: no block line, but the
# status text and the display count keep it - matching what do_disk
# stores today.
out=$(feed_client NORRDDISKS='^/data')
assert_contains "/var 50:500" "$out" "unfiltered filesystem still in the block"
assert_not_contains "/data 90:900" "$out" "NORRDDISKS-filtered filesystem has no block line"
assert_contains "<!-- linecount=2 -->" "$out" "display count unaffected by storage filters"

# RRDDISKS keeps only the matching filesystems.
out=$(feed_client RRDDISKS='^/var')
assert_contains "/var 50:500" "$out" "RRDDISKS-matched filesystem in the block"
assert_not_contains "/data 90:900" "$out" "non-matching filesystem has no block line"

pass "unix_disk_report emits the self-describing disk METRICS block"
