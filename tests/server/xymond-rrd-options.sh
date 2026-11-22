#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/xymond-rrd-options.sh
#
# xymond_rrd's --flushtimeout and --cachemultiplier take a checked number:
# a value out of range or not a number is reported and ignored, and
# xymond_rrd goes on with its default. --flushtimeout=-1 used to reach
# select() as a negative timeout, which fails, and xymond_rrd stopped before
# handling a single message; it is now ignored, and the update is written.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
if [ ! -f "$ROOT/include/config.h" ]; then
	skip "tree not built (run make first; the post-build CI suite covers this)"
fi
require_bin XYMOND_RRD xymond/xymond_rrd

work=$(mktempdir)
mkdir -p "$work/home/etc" "$work/tmp" "$work/run" "$work/rrd"
: > "$work/home/etc/analysis.cfg"
: > "$work/hosts.cfg"

# run OPTION... -- xymond_rrd with OPTIONs, fed one inode status for
# unix.test, until it has read all of its input.
run() {
	local ts
	ts=$(date +%s)
	{
		printf '@@status#1|%s|127.0.0.1||unix.test|inode|%s|green||||||||||unix|\n' "$ts" "$((ts + 1800))"
		printf 'status unix.test.inode green %s - Filesystems ok\n' "$ts"
		printf 'Filesystem itotal iused ifree %%iused Mounted on\n'
		printf '/dev/ld0a 259070 41020 218050 15%% /\n@@\n'
	} | XYMONHOME="$work/home" XYMONVAR="$work" XYMONTMP="$work/tmp" \
	    XYMONRUNDIR="$work/run" XYMONRRDS="$work/rrd" HOSTSCFG="!$work/hosts.cfg" \
	    TEST2RRD="inode" GRAPHS="inode" \
		"$XYMOND_RRD" --rrddir="$work/rrd" --no-cache "$@" >"$work/rrd.log" 2>&1 || true
}

run --flushtimeout=-1
[ -f "$work/rrd/unix.test/inode,root.rrd" ] \
	|| fail "with --flushtimeout=-1, xymond_rrd wrote nothing: $(cat "$work/rrd.log")"
assert_contains "Ignoring --flushtimeout=-1" "$(cat "$work/rrd.log")" "a negative --flushtimeout is reported and ignored"

for opt in --flushtimeout=0 --flushtimeout=abc --flushtimeout=5x \
	   --cachemultiplier=0 --cachemultiplier=abc --cachemultiplier=999999999999; do
	rm -rf "$work/rrd/unix.test"
	run "$opt"
	[ -f "$work/rrd/unix.test/inode,root.rrd" ] \
		|| fail "with $opt, xymond_rrd wrote nothing: $(cat "$work/rrd.log")"
	assert_contains "Ignoring $opt" "$(cat "$work/rrd.log")" "$opt is reported and ignored"
done

rm -rf "$work/rrd/unix.test"
run --flushtimeout=30 --cachemultiplier=2
[ -f "$work/rrd/unix.test/inode,root.rrd" ] \
	|| fail "with valid values, xymond_rrd wrote nothing: $(cat "$work/rrd.log")"
assert_not_contains "Ignoring" "$(cat "$work/rrd.log")" "valid values are taken without a complaint"

pass "xymond_rrd reports and ignores a --flushtimeout or --cachemultiplier that is not a number in range, and still writes its updates"
