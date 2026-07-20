#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/web/showgraph-aggregate-sum.sh
#
# Aggregate tokens in a gdef: "@SUMNAN:t@" expands to the RPN summing
# the per-file DEFs across every matched RRD file, and plain lines in
# such a gdef are emitted once instead of once per file. Runs the real
# pipeline: files created by xymond_rrd from an XYMON METRICS block,
# then rendered by the real showgraph.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)

CC=${CC:-cc}
command -v "$CC" >/dev/null 2>&1 || skip "no C compiler available (CC=$CC)"
command -v make >/dev/null 2>&1 || skip "make not available"

require_bin XYMOND_RRD "xymond/xymond_rrd"

# These sections assert eager file creation; the default is lazy.
export LAZYDEFAULT=off

[ -f "$ROOT/include/config.h" ] && [ -f "$ROOT/lib/libxymoncomm.a" ] \
	|| skip "tree not built (run make first; the post-build CI suite covers this)"
[ -f "$ROOT/web/showgraph.cgi" ] || skip "tree built without RRD support (no showgraph.cgi)"

rrddef=$(sed -n 's/^RRDDEF *= *//p' "$ROOT/Makefile")
rrdlibs=$(sed -n 's/^RRDLIBS *= *//p' "$ROOT/Makefile")
[ -n "$rrdlibs" ] || rrdlibs="-lrrd"
ssllibs=$(sed -n 's/^SSLLIBS *= *//p' "$ROOT/Makefile")

pcre_libs=${PCRELIBS:-}
if [ -z "$pcre_libs" ] && command -v pkg-config >/dev/null 2>&1; then
	pcre_libs=$(pkg-config --libs libpcre2-8 2>/dev/null || true)
fi
[ -n "$pcre_libs" ] || pcre_libs="-lpcre2-8"

work=$(mktemp -d "${TMPDIR:-/tmp}/xymon-aggsum.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM

[ -w "$ROOT/lib" ] || skip "source tree not writable (cannot refresh libxymoncomm.a)"
make -C "$ROOT/lib" libxymoncomm.a >"$work/libbuild.log" 2>&1 \
	|| { cat "$work/libbuild.log" >&2; fail "cannot refresh libxymoncomm.a"; }

"$CC" -I"$ROOT/include" -I"$ROOT/lib" $rrddef -o "$work/showgraph" \
	"$ROOT/web/showgraph.c" "$ROOT/lib/libxymoncomm.a" \
	$pcre_libs $rrdlibs $ssllibs 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; fail "showgraph does not compile"; }

# Two instance files, created the way the feature does
rrds="$work/rrd/testhost"
mkdir -p "$work/rrd" "$work/tmp"
ts=$(date +%s)
{
	printf '@@status|%s|127.0.0.1|origin|testhost|diskio|%s|green||green|%s|0||0||%s|0|linux|/\n' \
		"$ts" $((ts+1800)) "$ts" "$ts"
	printf '<!--XYMON METRICS: diskio_ops\n'
	printf 'DS:reads:GAUGE:600:0:U DS:writes:GAUGE:600:0:U\n'
	printf 'ada0 10:20\n'
	printf 'ada1 5:6\n'
	printf -- '-->\n'
	printf 'Disk I/O Status\n'
	printf '@@\n'
} | env XYMONHOME="$work" XYMONTMP="$work/tmp" \
	"$XYMOND_RRD" --rrddir="$work/rrd" --no-cache 2>/dev/null
[ -f "$rrds/diskio_ops.ada0.rrd" ] && [ -f "$rrds/diskio_ops.ada1.rrd" ] \
	|| fail "xymond_rrd did not create the METRICS block files"

cp "$ROOT/xymond/etcfiles/graphs.cfg.DIST" "$work/graphs.cfg"
cat >>"$work/graphs.cfg" <<'EOF'

[diskio_ops_total]
	FNPATTERN ^diskio_ops\.(.+)\.rrd
	TITLE Total disk operations
	YAXIS Operations/sec
	DEF:rt@RRDIDX@=@RRDFN@:reads:AVERAGE
	DEF:wt@RRDIDX@=@RRDFN@:writes:AVERAGE
	CDEF:rtotal=@SUMNAN:rt@
	CDEF:wtotal=@SUMNAN:wt@
	LINE2:rtotal#00CC00:total reads
	LINE1:wtotal#0000FF:total writes
	-l 0
EOF

render() {  # render <query-extras> -> $work/out
	REQUEST_METHOD=GET \
	QUERY_STRING="host=testhost&service=diskio_ops_total&graph=hourly&action=view$1" \
	XYMONHOME="$work" \
		"$work/showgraph" --debug --config="$work/graphs.cfg" \
		--rrddir="$rrds" >"$work/out" 2>&1 || true
}

# Full render: one DEF per file, one summed CDEF each,
# draw statements exactly once, and a real PNG out of rrdtool.
render ""
grep -aq "=diskio_ops.ada0.rrd:reads:AVERAGE" "$work/out" || fail "missing per-file DEF for ada0/reads"
grep -aq "=diskio_ops.ada1.rrd:reads:AVERAGE" "$work/out" || fail "missing per-file DEF for ada1/reads"
grep -aq "CDEF:rtotal=rt0,rt1,ADDNAN" "$work/out" \
	|| fail "missing chained sum CDEF: $(grep -a CDEF "$work/out" | head -3)"
grep -aq "CDEF:wtotal=wt0,wt1,ADDNAN" "$work/out" \
	|| fail "missing second aggregate CDEF"
[ "$(grep -ac "LINE2:rtotal#00CC00:total reads" "$work/out")" = "1" ] \
	|| fail "draw statement not emitted exactly once"
grep -aq "Content-type: image/png" "$work/out" \
	|| fail "rrdtool rejected the generated arguments: $(grep -a 'ERROR\|error' "$work/out" | head -3)"

# A sliced render sums only the files in the window - by design, so a
# paged aggregate never silently mixes windows.
render "&first=1&count=1"
grep -a "CDEF:rtotal=" "$work/out" | grep -aq "rt0" \
	|| fail "sliced render lost its window's file"
grep -a "CDEF:rtotal=" "$work/out" | grep -aq "rt1" \
	&& fail "sliced render must aggregate only its window"

# Runtime @DSIDX@ over MULTIPLE files: templated and per-RRD-context
# lines are emitted per file, expanded with that file's DS count;
# anything else - plain CDEFs, comments - exactly once, or the
# duplicated vnames would make rrd_graph reject the whole graph.
{
	printf '@@status|%s|127.0.0.1|origin|testhost|diskio|%s|green||green|%s|0||0||%s|0|linux|/\n' \
		"$ts" $((ts+1800)) "$ts" "$ts"
	printf '<!--XYMON METRICS: pingm\n'
	printf 'DS:s1:GAUGE:600:0:U DS:s2:GAUGE:600:0:U\n'
	printf 'alpha 1:2\n'
	printf 'beta 3:4\n'
	printf -- '-->\n'
	printf 's\n@@\n'
} | env XYMONHOME="$work" XYMONTMP="$work/tmp" \
	"$XYMOND_RRD" --rrddir="$work/rrd" --no-cache 2>/dev/null
[ -f "$rrds/pingm.alpha.rrd" ] && [ -f "$rrds/pingm.beta.rrd" ] \
	|| fail "xymond_rrd did not create the runtime-dsidx files"

cat >>"$work/graphs.cfg" <<'EOF'

[pingm_rt]
	FNPATTERN ^pingm\.(.+)\.rrd
	TITLE Runtime dsidx
	YAXIS ms
	DEF:p@RRDIDX@x@DSIDX@=@RRDFN@:s@DSIDX@:AVERAGE
	LINE1:p@RRDIDX@x@DSIDX@#@COLOR@:@RRDPARAM@ s@DSIDX@
	CDEF:kzero=p0x1,0,*
	LINE1:kzero#000000:zero
	COMMENT:emitted-once
EOF

REQUEST_METHOD=GET \
QUERY_STRING="host=testhost&service=pingm_rt&graph=hourly&action=view" \
XYMONHOME="$work" \
	"$work/showgraph" --debug --config="$work/graphs.cfg" \
	--rrddir="$rrds" >"$work/out" 2>&1 || true
grep -aq "DEF:p0x1=" "$work/out" || fail "runtime dsidx: missing file-0 dataset-1 DEF"
grep -aq "DEF:p0x2=" "$work/out" || fail "runtime dsidx: missing file-0 dataset-2 DEF"
grep -aq "DEF:p1x1=" "$work/out" || fail "runtime dsidx: missing file-1 dataset-1 DEF"
grep -aq "DEF:p1x2=" "$work/out" || fail "runtime dsidx: missing file-1 dataset-2 DEF"
[ "$(grep -ac "CDEF:kzero=" "$work/out")" = "1" ] \
	|| fail "plain CDEF must be emitted exactly once, not per file"
[ "$(grep -ac "COMMENT:emitted-once" "$work/out")" = "1" ] \
	|| fail "COMMENT must be emitted exactly once, not per file"
grep -aq "Content-type: image/png" "$work/out" \
	|| fail "runtime-dsidx multi-file graph rejected by rrdtool: $(grep -a 'ERROR\|error' "$work/out" | head -3)"

# Unequal DS counts across matched files: an emit-once within-file
# aggregate must reference only datasets EVERY file's DEFs define (the
# smallest per-file count) - the largest count would reference datasets
# the smaller file never DEFs and rrd_graph would reject the whole image.
{
	printf '@@status|%s|127.0.0.1|origin|testhost|diskio|%s|green||green|%s|0||0||%s|0|linux|/\n' \
		"$ts" $((ts+1800)) "$ts" "$ts"
	printf '<!--XYMON METRICS: pingm\n'
	printf 'DS:s1:GAUGE:600:0:U\n'
	printf 'gamma 9\n'
	printf -- '-->\n'
	printf 's\n@@\n'
} | env XYMONHOME="$work" XYMONTMP="$work/tmp" \
	"$XYMOND_RRD" --rrddir="$work/rrd" --no-cache 2>/dev/null
[ -f "$rrds/pingm.gamma.rrd" ] || fail "single-DS instance not created"

cat >>"$work/graphs.cfg" <<'EOF'

[pingm_med]
	FNPATTERN ^pingm\.(.+)\.rrd
	TITLE Runtime dsidx median
	YAXIS ms
	DEF:p@RRDIDX@x@DSIDX@=@RRDFN@:s@DSIDX@:AVERAGE
	LINE1:p@RRDIDX@x@DSIDX@#@COLOR@:@RRDPARAM@ s@DSIDX@
	CDEF:med=@DSMEDIAN:p2x@
	LINE1:med#000000:median
EOF

REQUEST_METHOD=GET \
QUERY_STRING="host=testhost&service=pingm_med&graph=hourly&action=view" \
XYMONHOME="$work" \
	"$work/showgraph" --debug --config="$work/graphs.cfg" \
	--rrddir="$rrds" >"$work/out" 2>&1 || true
grep -aq "DEF:p0x2=" "$work/out" || fail "larger file lost its second dataset"
grep -aq "CDEF:med=p2x1,1,MEDIAN" "$work/out" \
	|| fail "emit-once aggregate not clamped to the smallest per-file DS count: $(grep -a CDEF:med "$work/out")"
grep -aq "disagree on DS count" "$work/out" \
	|| fail "DS-count clamp not visible in the debug output"
grep -aq "Content-type: image/png" "$work/out" \
	|| fail "unequal-DS-count graph rejected by rrdtool: $(grep -a 'ERROR\|error' "$work/out" | head -3)"

pass "aggregate tokens sum across matched files into one CDEF"
