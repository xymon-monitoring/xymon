#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Markus Häll

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin XYMOND_RRD xymond/xymond_rrd

ROOT=$(find_root)
require_c_buildenv "$ROOT"

work=$(mktempdir)
mkdir -p "$work/home/etc" "$work/tmp" "$work/rrd"
: > "$work/home/etc/analysis.cfg"
: > "$work/hosts.cfg"

# The configured RRD flags through the helpers, which also answer for a
# build with no top-level Makefile (tests/README.md).
rrddef=$(xymon_makevar "$ROOT" RRDDEF)
rrdincdir=$(xymon_makevar "$ROOT" RRDINCDIR)
rrdlibs=$(xymon_makevar "$ROOT" RRDLIBS)
[ -n "$rrdlibs" ] || rrdlibs="-lrrd"
harness_ldflags=$(xymon_ldflags "$ROOT")

# Configured flags are deliberate word-split lists.
# shellcheck disable=SC2086
"$CC" -iquote "$ROOT/include" $rrddef $rrdincdir \
	-o "$work/rrd-lastupdate" \
	"$ROOT/tests/rrd/inode-unix-columns-harness.c" $rrdlibs $harness_ldflags \
	2>"$work/cc.log" || {
	cat "$work/cc.log" >&2
	fail "librrd last-update harness does not compile"
}

timestamp=$(date +%s)
message="@@status|$timestamp|127.0.0.1||unix.test|inode|$((timestamp + 1800))|green||||||||||unix|
status unix.test.inode green $timestamp - Filesystems ok
Filesystem itotal iused ifree %iused Mounted on
/dev/ld0a 259070 41020 218050 15% /
@@
"

printf '%s' "$message" | env \
	XYMONHOME="$work/home" \
	XYMONVAR="$work" \
	XYMONTMP="$work/tmp" \
	XYMONRUNDIR="$work/tmp" \
	XYMONRRDS="$work/rrd" \
	HOSTSCFG="!$work/hosts.cfg" \
	TEST2RRD="inode" \
	GRAPHS="inode" \
	"$XYMOND_RRD" --no-cache --rrddir="$work/rrd" \
	>"$work/xymond_rrd.log" 2>&1

rrd="$work/rrd/unix.test/inode,root.rrd"
assert_file_exists "$rrd" "UNIX-column root inode RRD"

lastupdate=$("$work/rrd-lastupdate" "$rrd")
assert_match 'pct[[:space:]]+used' "$lastupdate" \
	"inode RRD defines percentage and used-inode data sources"
assert_match "${timestamp}:[[:space:]]+15([.]0+)?[[:space:]]+41020([.]0+)?" "$lastupdate" \
	"UNIX columns store percentage and used-inode values"

pass "UNIX inode columns reach the shared RRD pipeline"
