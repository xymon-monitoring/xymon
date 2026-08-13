#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/web/html-log-trendfilter-linecount.sh
#
# Status-page graph paging must follow the filtered RRD file set. The old
# line-count heuristic parses status text; RRDEXCLUDE/RRDINCLUDE must instead
# count graphable RRD files so the behavior is generic across graph types.

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

mkdir -p "$work/etc" "$work/rrd/testhost"
cat >"$work/etc/graphs.cfg" <<'EOF'
[disk]
	FNPATTERN ^disk(.*).rrd
	TITLE Disk
	YAXIS pct
	DEF:p@RRDIDX@=@RRDFN@:pct:AVERAGE
	LINE2:p@RRDIDX@#@COLOR@:@RRDPARAM@
EOF

touch "$work/rrd/testhost/disk,root.rrd"
for n in 1 2 3 4 5; do touch "$work/rrd/testhost/disk,zdata,fs$n.rrd"; done
touch "$work/rrd/testhost/disk,poudriere,data1.rrd" "$work/rrd/testhost/disk,poudriere,data2.rrd"

run_case() {  # run_case <mode> [ENV=val...]
	mode=$1; shift
	env XYMONHOME="$work" XYMONRRDS="$work/rrd" CGIBINURL="/xymon-cgi" \
	    RRDWIDTH=576 RRDHEIGHT=120 XYMONSKIN="/xymon/gifs" XYMONWEB="/xymon" \
	    IMAGEFILETYPE="gif" TEST2RRD="disk" GRAPHS="disk" \
	    INFOCOLUMN="info" TRENDSCOLUMN="trends" ACKUNTILMSG="until %H:%M" \
	    "$@" "$work/harness" "$mode" >"$work/out.log" 2>"$work/err.log" \
		|| fail "scenario '$mode' failed: $(cat "$work/err.log")"
}

run_case none
run_case excl RRDEXCLUDE='disk:poudriere'
run_case incl RRDINCLUDE='disk:^disk,zdata'
run_case root RRDEXCLUDE='disk:^disk,root$'

pass "status-page graph paging follows RRDEXCLUDE/RRDINCLUDE-filtered RRD files"
