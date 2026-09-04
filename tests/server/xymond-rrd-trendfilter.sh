#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/xymond-rrd-trendfilter.sh
#
# Guard for the generic per-RRD-file RRDEXCLUDE/RRDINCLUDE trending filter
# (issue #244): feed real messages to the built xymond_rrd over stdin and
# assert which RRD files get created. Entries are "scope:regex"; scope can be
# the test name, the RRD filename prefix, or "*" for all files, and regexes
# match the RRD filename minus ".rrd".

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin XYMOND_RRD "xymond/xymond_rrd"

work=$(mktempdir)

feed_iostat() {  # feed_iostat [ENV=val...] -- send one 3-device iostat data message
	local ts; ts=$(date +%s)
	rm -rf "$work/rrd"; mkdir -p "$work/rrd" "$work/tmp"
	{
		printf '@@data|%s|127.0.0.1|origin|testhost|iostat|linux|/\n' "$ts"
		printf 'BEGINKEY\n'
		printf 'd0 /\n'
		printf 'd15 /scratch/a\n'
		printf 'd16 /scratch/b\n'
		printf 'ENDKEY\n'
		printf 'BEGINDATA\n'
		printf '    r/s    w/s   kr/s   kw/s wait actv wsvc_t asvc_t  %%w  %%b s/w h/w trn tot device\n'
		printf '    0.9    2.8    7.3    1.8  0.0  0.0    2.7    9.3   1   2   0   0   0   0 d0\n'
		printf '    0.1    0.3    0.8    0.5  0.0  0.0    5.2   11.0   0   0   0   0   0   0 d15\n'
		printf '    0.2    0.4    0.9    0.6  0.0  0.0    5.3   12.0   0   0   0   0   0   0 d16\n'
		printf 'ENDDATA\n'
		printf '@@\n'
	} | env XYMONHOME="$work" XYMONTMP="$work/tmp" "$@" \
		"$XYMOND_RRD" --rrddir="$work/rrd" --no-cache 2>/dev/null
	ls "$work/rrd/testhost" 2>/dev/null || true
}

feed_http() {  # feed_http [ENV=val...] -- send one http status message
	local ts; ts=$(date +%s)
	rm -rf "$work/rrd"; mkdir -p "$work/rrd" "$work/tmp"
	{
		printf '@@status|%s|127.0.0.1|origin|testhost|http|%s|green||green|%s|0||0||%s|0|linux|/\n' \
			"$ts" $((ts+1800)) "$ts" "$ts"
		printf 'green ok\n'
		printf '&green http://example.com/path - OK\n'
		printf 'Seconds: 0.12\n'
		printf '@@\n'
	} | env XYMONHOME="$work" XYMONTMP="$work/tmp" "$@" \
		"$XYMOND_RRD" --rrddir="$work/rrd" --no-cache 2>/dev/null
	ls "$work/rrd/testhost" 2>/dev/null || true
}

out=$(feed_iostat)
assert_contains "iostat.,scratch,a.rrd" "$out" "no filter: every iostat instance is trended"
assert_contains "iostat.,scratch,b.rrd" "$out" "no filter: every iostat instance is trended"

out=$(feed_iostat RRDEXCLUDE='iostat:scratch,a')
assert_not_contains "iostat.,scratch,a.rrd" "$out" "RRDEXCLUDE drops the matching iostat file"
assert_contains "iostat.,scratch,b.rrd" "$out" "RRDEXCLUDE leaves the other iostat files"

out=$(feed_iostat RRDINCLUDE='iostat:scratch')
assert_contains "iostat.,scratch,a.rrd" "$out" "RRDINCLUDE keeps matching iostat files"
assert_contains "iostat.,scratch,b.rrd" "$out" "RRDINCLUDE keeps matching iostat files"
assert_not_contains "iostat.,root.rrd" "$out" "RRDINCLUDE drops non-matching iostat files"

out=$(feed_iostat RRDINCLUDE='iostat:.' RRDEXCLUDE='iostat:scratch,b')
assert_not_contains "iostat.,scratch,b.rrd" "$out" "RRDEXCLUDE wins over a matching RRDINCLUDE"
assert_contains "iostat.,scratch,a.rrd" "$out" "the include side still applies"

# Scope can be global, and regexes may contain commas because entries are
# whitespace-separated.
out=$(feed_iostat RRDEXCLUDE='*:iostat.,scratch,a iostat:iostat.,scratch,b')
assert_not_contains "scratch,a" "$out" "global scope entry applies"
assert_not_contains "scratch,b" "$out" "test scope entry applies"
assert_contains "iostat.,root.rrd" "$out" "unmatched instances survive multi-entry filters"

out=$(feed_iostat RRDEXCLUDE='ifstat:scratch')
assert_contains "iostat.,scratch,a.rrd" "$out" "an RRDEXCLUDE entry for another scope does not apply"

out=$(feed_http)
assert_contains "tcp.http.example.com,path.rrd" "$out" "http status creates an RRD under the tcp filename scope"

out=$(feed_http RRDEXCLUDE='tcp:^tcp.http.example')
assert_not_contains "tcp.http.example.com,path.rrd" "$out" "RRDEXCLUDE can use the RRD filename prefix as scope"

pass "RRDEXCLUDE/RRDINCLUDE per-RRD-file filter (exclude, include-only, precedence, wildcard, per-scope)"
