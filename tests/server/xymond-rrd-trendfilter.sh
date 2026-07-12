#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/xymond-rrd-trendfilter.sh
#
# Guard for the generic RRDEXCLUDE/RRDINCLUDE trending filter (issue #244): feed a
# real @@status disk message to the built xymond_rrd over stdin and assert
# which RRD files get created. Entries are "testname:regex" matched against
# the RRD filename minus ".rrd" (e.g. "disk,poudriere,data1").

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin XYMOND_RRD "xymond/xymond_rrd"

work=$(mktempdir)

feed() {  # feed [ENV=val...] -- send one 3-filesystem disk status
	local ts; ts=$(date +%s)
	rm -rf "$work/rrd"; mkdir -p "$work/rrd" "$work/tmp"
	{
		printf '@@status|%s|127.0.0.1|origin|testhost|disk|%s|green||green|%s|0||0||%s|0|linux|/\n' \
			"$ts" $((ts+1800)) "$ts" "$ts"
		printf 'green ok\n\nFilesystem 1024-blocks Used Avail Capacity Mounted on\n'
		printf '/dev/da0 100 50 50 50%% /\n'
		printf 'zdata/photos 100 50 50 50%% /zdata/photos\n'
		printf 'zdata/poud 100 50 50 50%% /poudriere/data1\n'
		printf '@@\n'
	} | env XYMONHOME="$work" XYMONTMP="$work/tmp" "$@" \
		"$XYMOND_RRD" --rrddir="$work/rrd" --no-cache 2>/dev/null
	ls "$work/rrd/testhost" 2>/dev/null || true
}

out=$(feed)
assert_contains "disk,poudriere,data1.rrd" "$out" "no filter: every filesystem is trended"

out=$(feed RRDEXCLUDE='disk:poudriere')
assert_not_contains "poudriere" "$out" "RRDEXCLUDE drops the matching instance"
assert_contains "disk,zdata,photos.rrd" "$out" "RRDEXCLUDE leaves the others"

out=$(feed RRDINCLUDE='disk:^disk,zdata')
assert_contains "disk,zdata,photos.rrd" "$out" "RRDINCLUDE keeps matching instances"
assert_not_contains "poudriere" "$out" "RRDINCLUDE drops non-matching instances"
assert_not_contains "disk.rrd" "$out" "RRDINCLUDE drops the root fs too"

out=$(feed RRDINCLUDE='disk:.' RRDEXCLUDE='disk:poudriere')
assert_not_contains "poudriere" "$out" "RRDEXCLUDE wins over a matching RRDINCLUDE"
assert_contains "disk,zdata,photos.rrd" "$out" "the include side still applies"

# Whitespace separates entries; regexes may contain commas (instance names do)
out=$(feed RRDEXCLUDE='disk:^disk,poudriere disk:zdata,photos')
assert_not_contains "poudriere" "$out" "first of two entries applies"
assert_not_contains "photos" "$out" "second of two entries applies"
assert_contains "disk,root.rrd" "$out" "unmatched instances survive multi-entry filters"

out=$(feed RRDEXCLUDE='iostat:poudriere')
assert_contains "disk,poudriere,data1.rrd" "$out" "a RRDEXCLUDE entry for another test does not apply"

pass "RRDEXCLUDE/RRDINCLUDE generic trending filter (exclude, include-only, precedence, per-test scoping)"
