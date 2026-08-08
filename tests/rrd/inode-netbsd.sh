#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later

set -euo pipefail

. "$(dirname "$0")/../lib/assert.sh"

require_bin XYMOND_RRD xymond/xymond_rrd
command -v rrdtool >/dev/null 2>&1 || skip "rrdtool not found"

work=$(mktempdir)
mkdir -p "$work/home/etc" "$work/tmp" "$work/rrd"
: > "$work/home/etc/analysis.cfg"
: > "$work/hosts.cfg"

timestamp=$(date +%s)
message="@@status|$timestamp|127.0.0.1||netbsd.test|inode|$((timestamp + 1800))|green||||||||||netbsd|
status netbsd.test.inode green $timestamp - Filesystems ok
Filesystem itotal iused ifree %iused Mounted on
/dev/ld0a 259070 41020 218050 15% /
@@
"

printf '%s' "$message" | env \
	XYMONHOME="$work/home" \
	XYMONVAR="$work" \
	XYMONTMP="$work/tmp" \
	XYMONRRDS="$work/rrd" \
	HOSTSCFG="$work/hosts.cfg" \
	TEST2RRD="inode" \
	GRAPHS="inode" \
	"$XYMOND_RRD" --no-cache --rrddir="$work/rrd" \
	>"$work/xymond_rrd.log" 2>&1

rrd="$work/rrd/netbsd.test/inode,root.rrd"
assert_file_exists "$rrd" "NetBSD root inode RRD"

lastupdate=$(rrdtool lastupdate "$rrd")
assert_match 'pct[[:space:]]+used' "$lastupdate" \
	"inode RRD defines percentage and used-inode data sources"
assert_match "${timestamp}:[[:space:]]+15([.]0+)?[[:space:]]+41020([.]0+)?" "$lastupdate" \
	"inode RRD stores NetBSD percentage and used-inode values"

pass "NetBSD inode status reaches the shared RRD pipeline"
