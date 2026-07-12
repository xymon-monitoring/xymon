#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/web/html-log-trendfilter-linecount.sh
#
# The generic RRDEXCLUDE/RRDINCLUDE trending filter (issue #244) must be honoured by
# the disk-family graph paging: a filesystem those settings keep out of the
# RRDs must not claim a paging slot (it would render as a broken image).
# Drives the real generate_html_log() through a C harness, one process per
# scenario (the patterns are compiled once per process). The legacy
# NORRDDISKS/RRDDISKS settings are not involved here.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
here=$(dirname "$0")

CC=${CC:-cc}
command -v "$CC" >/dev/null 2>&1 || skip "no C compiler available (CC=$CC)"
command -v make >/dev/null 2>&1 || skip "make not available"

pcre_libs=${PCRELIBS:-}
if [ -z "$pcre_libs" ] && command -v pkg-config >/dev/null 2>&1; then
	pcre_libs=$(pkg-config --libs libpcre2-8 2>/dev/null || true)
fi
[ -n "$pcre_libs" ] || pcre_libs="-lpcre2-8"

ssllibs=$(sed -n 's/^SSLLIBS *= *//p' "$ROOT/Makefile" 2>/dev/null || true)

work=$(mktemp -d "${TMPDIR:-/tmp}/xymon-trendfilter-lc.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM

[ -f "$ROOT/include/config.h" ] && [ -f "$ROOT/lib/libxymoncomm.a" ] \
	|| skip "tree not built (run make first; the post-build CI suite covers this)"
make -C "$ROOT/lib" libxymoncomm.a >"$work/libbuild.log" 2>&1 \
	|| { cat "$work/libbuild.log" >&2; fail "cannot refresh libxymoncomm.a"; }

"$CC" -I"$ROOT/include" -I"$ROOT/lib" -o "$work/harness" \
	"$here/html-log-trendfilter-linecount-harness.c" "$ROOT/lib/libxymoncomm.a" \
	$pcre_libs $ssllibs 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; fail "harness does not compile"; }

run_case() {  # run_case <mode> [ENV=val...]
	mode=$1; shift
	env XYMONHOME="$work" CGIBINURL="/xymon-cgi" RRDWIDTH=576 RRDHEIGHT=120 \
	    XYMONSKIN="/xymon/gifs" XYMONWEB="/xymon" IMAGEFILETYPE="gif" \
	    TEST2RRD="disk" GRAPHS="la,disk" \
	    INFOCOLUMN="info" TRENDSCOLUMN="trends" ACKUNTILMSG="until %H:%M" \
	    "$@" "$work/harness" "$mode" >"$work/out.log" 2>"$work/err.log" \
		|| fail "scenario '$mode' failed: $(cat "$work/err.log")"
}

run_case none
run_case excl RRDEXCLUDE='disk:poudriere'
run_case incl RRDINCLUDE='disk:^disk,zdata,fs'
run_case both RRDINCLUDE='disk:.' RRDEXCLUDE='disk:zdata,fs1$'
run_case root RRDEXCLUDE='disk:^disk,root$'

pass "disk-page graph paging honours the generic RRDEXCLUDE/RRDINCLUDE filter"
