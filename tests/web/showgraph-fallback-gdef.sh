#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/web/showgraph-fallback-gdef.sh
#
# Self-describing statuses, end to end: an XYMON METRICS block fed through
# the real xymond_rrd creates <name>.<instance>.rrd files, and showgraph
# must then graph them without any graphs.cfg entry - when no [name] gdef
# exists it synthesizes a generic one from the dataset names of the first
# matching file.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)

CC=${CC:-cc}
command -v "$CC" >/dev/null 2>&1 || skip "no C compiler available (CC=$CC)"
command -v make >/dev/null 2>&1 || skip "make not available"

require_bin XYMOND_RRD "xymond/xymond_rrd"


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

work=$(mktemp -d "${TMPDIR:-/tmp}/xymon-showgraph-fb.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM

[ -w "$ROOT/lib" ] || skip "source tree not writable (cannot refresh libxymoncomm.a)"
make -C "$ROOT/lib" libxymoncomm.a >"$work/libbuild.log" 2>&1 \
	|| { cat "$work/libbuild.log" >&2; fail "cannot refresh libxymoncomm.a"; }

"$CC" -I"$ROOT/include" -I"$ROOT/lib" $rrddef -o "$work/showgraph" \
	"$ROOT/web/showgraph.c" "$ROOT/lib/libxymoncomm.a" \
	$pcre_libs $rrdlibs $ssllibs 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; fail "showgraph does not compile"; }

# Create the RRD files the way the feature does: an XYMON METRICS block
# fed to xymond_rrd on a column with no TEST2RRD mapping.
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

render() {  # render <service> -> $work/out (PNG + debug dump interleaved)
	REQUEST_METHOD=GET \
	QUERY_STRING="host=testhost&service=$1&graph=hourly&action=view" \
	XYMONHOME="$work" \
		"$work/showgraph" --debug --config="$work/graphs.cfg" \
		--rrddir="$rrds" >"$work/out" 2>&1 || true
}

# No [diskio_ops] section exists in graphs.cfg: the synthesized gdef must
# find both files and one DEF+LINE per dataset, and produce a real PNG.
render "diskio_ops"
grep -aq "DEF:v0@RRDIDX@=@RRDFN@:reads:AVERAGE" "$work/out" 2>/dev/null \
	|| grep -aq "=diskio_ops.ada0.rrd:reads:AVERAGE" "$work/out" \
	|| fail "no generated DEF for dataset 'reads': $(grep -a 'DEF\|error\|Unknown' "$work/out" | head -5)"
grep -aq "=diskio_ops.ada1.rrd:writes:AVERAGE" "$work/out" \
	|| fail "second instance file not matched by the synthesized pattern"
grep -aq "Content-type: image/png" "$work/out" \
	|| fail "fallback did not render a PNG: $(grep -a 'error\|Unknown\|No RRD' "$work/out" | head -5)"

# A name with no matching RRD files fails cleanly, not with a crash.
render "ghost_graph"
grep -aq "No RRD files match this graph" "$work/out" \
	|| fail "missing-files case not reported cleanly: $(head -3 "$work/out")"

# Hand-written gdefs still win: [la] exists in the stock graphs.cfg, so an
# la request must not use the synthesized pattern.
render "la"
grep -aq "diskio_ops" "$work/out" && fail "stock gdef contaminated by fallback"

# --emit-gdef: the runtime synthesizer doubles as a one-shot scaffold - it
# prints the gdef block it would use, for capture into graphs.d/.
env XYMONHOME="$work" "$work/showgraph" --emit-gdef=diskio_ops --rrddir="$work/rrd" \
	>"$work/gdef.out" 2>"$work/gdef.err" \
	|| fail "--emit-gdef exited nonzero: $(cat "$work/gdef.err")"
grep -q '^\[diskio_ops\]$' "$work/gdef.out" || fail "--emit-gdef: missing section header"
grep -q 'FNPATTERN \^diskio_ops' "$work/gdef.out" || fail "--emit-gdef: missing FNPATTERN"
grep -q 'DEF:v0@RRDIDX@=@RRDFN@:reads:AVERAGE' "$work/gdef.out" || fail "--emit-gdef: missing DEF for dataset 'reads'"
grep -q 'LINE1:v1@RRDIDX@#@COLOR@:@RRDPARAM@ writes' "$work/gdef.out" || fail "--emit-gdef: missing LINE for dataset 'writes'"

# The emitted block is valid config: captured into graphs.cfg it becomes a
# hand-written gdef and must render on its own (no synthesis fallback).
cat "$work/gdef.out" >>"$work/graphs.cfg"
render "diskio_ops"
grep -aq "Content-type: image/png" "$work/out" \
	|| fail "captured gdef does not render: $(grep -a 'error\|Unknown' "$work/out" | head -3)"

# Unknown fileset: a clean CLI error on stderr, not a crash or HTML page.
env XYMONHOME="$work" "$work/showgraph" --emit-gdef=nosuch --rrddir="$work/rrd" \
	>"$work/gdef2.out" 2>"$work/gdef2.err" && fail "--emit-gdef for a missing fileset must exit nonzero"
grep -q "No RRD files matching" "$work/gdef2.err" || fail "--emit-gdef missing-fileset error not on stderr"

# Declared units drive the synthesized YAXIS: a block whose DSes all carry
# "msec" (an alias) renders and scaffolds with YAXIS "ms" (the canonical
# spelling) - units flow producer -> writer -> fileset index -> renderer.
ts=$(date +%s)
{
	printf '@@status|%s|127.0.0.1|origin|testhost|diskio|%s|green||green|%s|0||0||%s|0|linux|/\n' \
		"$ts" $((ts+1800)) "$ts" "$ts"
	printf '<!--XYMON METRICS: resp\n'
	printf 'DS:rd:GAUGE:600:0:U:msec DS:wr:GAUGE:600:0:U:msec\n'
	printf 'api 42:187\n'
	printf -- '-->\ns\n@@\n'
} | env XYMONHOME="$work" XYMONTMP="$work/tmp" \
	"$XYMOND_RRD" --rrddir="$work/rrd" --no-cache 2>/dev/null
[ -f "$rrds/resp.api.rrd" ] || fail "unit-declaring block files not created"
grep -q 'resp\.api\.rrd [0-9]* u=rd:msec,wr:msec h=rd:600,wr:600 d=rd,wr g=[0-9]*$' "$work/rrd/testhost/.fileset-index" \
	|| fail "units not in the index: $(cat "$work/rrd/testhost/.fileset-index")"

env XYMONHOME="$work" XYMONRRDS="$work/rrd" "$work/showgraph" --emit-gdef=resp --rrddir="$work/rrd" \
	>"$work/gdefu.out" 2>"$work/gdefu.err" \
	|| fail "--emit-gdef with units exited nonzero: $(cat "$work/gdefu.err")"
grep -q '	YAXIS ms$' "$work/gdefu.out" \
	|| fail "YAXIS not derived from declared units (alias msec->ms): $(cat "$work/gdefu.out")"

# And the live render uses the same derived axis (present in the args dump).
REQUEST_METHOD=GET \
QUERY_STRING="host=testhost&service=resp&graph=hourly&action=view" \
XYMONHOME="$work" XYMONRRDS="$work/rrd" \
	"$work/showgraph" --debug --config="$work/graphs.cfg" \
	--rrddir="$rrds" >"$work/out" 2>&1 || true
grep -aq "Content-type: image/png" "$work/out" || fail "unit-labelled graph does not render"
# (--debug prefixes each dumped arg with pid+timestamp, so match suffixes)
grep -a -A1 -- ' -v$' "$work/out" | grep -aq ' ms$' \
	|| fail "derived YAXIS 'ms' missing from render args: $(grep -a -A1 -- ' -v$' "$work/out" | head -4)"

# The axis must be honest for the whole image: a second instance of the
# same fileset declaring DIFFERENT units (seconds vs milliseconds) drops
# the derivation - the image renders with the generic axis rather than
# labelling one curve with the other's unit.
{
	printf '@@status|%s|127.0.0.1|origin|testhost|diskio|%s|green||green|%s|0||0||%s|0|linux|/\n' \
		"$ts" $((ts+1800)) "$ts" "$ts"
	printf '<!--XYMON METRICS: resp\n'
	printf 'DS:rd:GAUGE:600:0:U:s DS:wr:GAUGE:600:0:U:s\n'
	printf 'api2 1:2\n'
	printf -- '-->\ns\n@@\n'
} | env XYMONHOME="$work" XYMONTMP="$work/tmp" \
	"$XYMOND_RRD" --rrddir="$work/rrd" --no-cache 2>/dev/null
[ -f "$rrds/resp.api2.rrd" ] || fail "second-unit instance not created"
grep -q 'resp\.api2\.rrd .* u=rd:s,wr:s' "$work/rrd/testhost/.fileset-index" \
	|| fail "second instance's units not recorded: $(grep resp.api2 "$work/rrd/testhost/.fileset-index")"
REQUEST_METHOD=GET \
QUERY_STRING="host=testhost&service=resp&graph=hourly&action=view" \
XYMONHOME="$work" XYMONRRDS="$work/rrd" \
	"$work/showgraph" --debug --config="$work/graphs.cfg" \
	--rrddir="$rrds" >"$work/out" 2>&1 || true
grep -aq "Content-type: image/png" "$work/out" || fail "mixed-unit fileset does not render"
grep -a -A1 -- ' -v$' "$work/out" | grep -aq ' ms$' \
	&& fail "mixed-unit fileset still labelled with the first file's axis"

# A "%" unit implies rrdtool options (--units-exponent 0), and the scaffold
# must carry them as a GRAPHOPTIONS line: a bare definition line would
# reload as ONE rrdtool argument, which rrd_graph rejects - the captured
# gdef must round-trip.
{
	printf '@@status|%s|127.0.0.1|origin|testhost|diskio|%s|green||green|%s|0||0||%s|0|linux|/\n' \
		"$ts" $((ts+1800)) "$ts" "$ts"
	printf '<!--XYMON METRICS: cpuuse\n'
	printf 'DS:usr:GAUGE:600:0:U:%% DS:sys:GAUGE:600:0:U:%%\n'
	printf 'cpu0 42:7\n'
	printf -- '-->\ns\n@@\n'
} | env XYMONHOME="$work" XYMONTMP="$work/tmp" \
	"$XYMOND_RRD" --rrddir="$work/rrd" --no-cache 2>/dev/null
[ -f "$rrds/cpuuse.cpu0.rrd" ] || fail "percent-unit block files not created"
env XYMONHOME="$work" XYMONRRDS="$work/rrd" "$work/showgraph" --emit-gdef=cpuuse --rrddir="$work/rrd" \
	>"$work/gdefp.out" 2>"$work/gdefp.err" \
	|| fail "--emit-gdef with %-unit exited nonzero: $(cat "$work/gdefp.err")"
grep -q '	GRAPHOPTIONS --units-exponent 0$' "$work/gdefp.out" \
	|| fail "derived graphopts not emitted as a GRAPHOPTIONS line: $(cat "$work/gdefp.out")"

# Round-trip: captured into graphs.cfg, the emitted text must reload into
# graphopts - separate rrdtool argv tokens - not into the definition lines.
cat "$work/gdefp.out" >>"$work/graphs.cfg"
REQUEST_METHOD=GET \
QUERY_STRING="host=testhost&service=cpuuse&graph=hourly&action=view" \
XYMONHOME="$work" XYMONRRDS="$work/rrd" \
	"$work/showgraph" --debug --config="$work/graphs.cfg" \
	--rrddir="$rrds" >"$work/out" 2>&1 || true
grep -aq -- '--units-exponent 0$' "$work/out" \
	&& fail "captured graphopts reached rrd_graph as one token"
grep -aq -- '--units-exponent$' "$work/out" \
	|| fail "captured graphopts missing from render args: $(grep -a 'exponent\|GRAPHOPT' "$work/out" | head -3)"
# (the image/png header is printed before rrd_graph runs, so also check
# that no option-parse error followed it)
grep -aq "invalid option" "$work/out" \
	&& fail "captured %-unit gdef rejected by rrd_graph: $(grep -a 'invalid option' "$work/out" | head -1)"
grep -aq "Content-type: image/png" "$work/out" \
	|| fail "captured %-unit gdef does not render: $(grep -a 'ERROR\|error' "$work/out" | head -3)"

# Declared THRESHOLD relations: the operand DS is a threshold curve, not a
# peer metric - threshold-styled on a single-instance image; a literal
# operand renders as an HRULE. THRESHOLDS OFF (a meta-only graphs.cfg
# section) suppresses the co-plot; the operand still never plots as a peer.
ts=$(date +%s)
{
	printf '@@status|%s|127.0.0.1|origin|testhost|diskio|%s|green||green|%s|0||0||%s|0|linux|/\n' \
		"$ts" $((ts+1800)) "$ts" "$ts"
	printf '<!--XYMON METRICS: thr\n'
	printf 'DS:val:GAUGE:600:0:U:ms DS:val_warn:GAUGE:600:0:U:ms\n'
	printf 'THRESHOLD:val:>val_warn:warn\n'
	printf 'THRESHOLD:val:>500\n'
	printf 'api 42:187\n'
	printf -- '-->\ns\n@@\n'
} | env XYMONHOME="$work" XYMONTMP="$work/tmp" \
	"$XYMOND_RRD" --rrddir="$work/rrd" --no-cache 2>/dev/null
[ -f "$rrds/thr.api.rrd" ] || fail "threshold-declaring block files not created"
grep -q 'thr\.api\.rrd [0-9]* u=val:ms,val_warn:ms h=val:600,val_warn:600 d=val,val_warn t=val:>val_warn:warn,val:>500:crit g=[0-9]*$' "$work/rrd/testhost/.fileset-index" \
	|| fail "threshold relations not in the index: $(grep thr.api "$work/rrd/testhost/.fileset-index")"

env XYMONHOME="$work" XYMONRRDS="$work/rrd" "$work/showgraph" --emit-gdef=thr --rrddir="$work/rrd" \
	>"$work/gdeft.out" 2>"$work/gdeft.err" \
	|| fail "--emit-gdef with thresholds exited nonzero: $(cat "$work/gdeft.err")"
grep -q 'LINE1:t0@RRDIDX@#FFCC00:val_warn warn' "$work/gdeft.out" \
	|| fail "threshold DS not threshold-styled: $(cat "$work/gdeft.out")"
grep -q 'HRULE:500#FF0000:val crit (500)' "$work/gdeft.out" \
	|| fail "literal threshold not an HRULE: $(cat "$work/gdeft.out")"
grep -q '#@COLOR@:@RRDPARAM@ val_warn' "$work/gdeft.out" \
	&& fail "threshold DS must not plot as a peer metric"

render_thr() {
	REQUEST_METHOD=GET \
	QUERY_STRING="host=testhost&service=thr&graph=hourly&action=view" \
	XYMONHOME="$work" XYMONRRDS="$work/rrd" \
		"$work/showgraph" --debug --config="$work/graphs.cfg" \
		--rrddir="$rrds" >"$work/out" 2>&1 || true
}
render_thr
grep -aq "Content-type: image/png" "$work/out" || fail "threshold graph does not render: $(grep -a 'ERROR' "$work/out" | head -2)"
grep -aq "HRULE:500#FF0000" "$work/out" || fail "HRULE missing from render args"

# The admin's say: THRESHOLDS OFF suppresses the co-plot. The meta-only
# section lives in the SAME file --config points at - the metadata reader
# must follow --config, not the default path.
cp "$work/graphs.cfg" "$work/graphs.cfg.bak"
printf '\n[thr]\n\tTHRESHOLDS OFF\n' >>"$work/graphs.cfg"
render_thr
grep -aq "Content-type: image/png" "$work/out" || fail "THRESHOLDS OFF graph does not render"
grep -aq "Invalid request" "$work/out" && fail "meta-only section broke the synthesis: $(grep -a 'body' "$work/out" | head -1)"
grep -aq "DEF:" "$work/out" || fail "meta-only section rendered no definitions"
grep -aq "FFCC00" "$work/out" && fail "THRESHOLDS OFF must suppress the threshold curves"
grep -aq "HRULE:500" "$work/out" && fail "THRESHOLDS OFF must suppress literal HRULEs too"
grep -aq "@RRDPARAM@ val_warn\|:val_warn$" "$work/out" && fail "operand still must not plot as a peer"
mv "$work/graphs.cfg.bak" "$work/graphs.cfg"

# A corrupt index relation (relop-less: the producer would reject it) must
# not suppress datasets: the renderer applies the producer's validation,
# and both DSes plot as peers.
sed 's/ t=val:>val_warn:warn,val:>500:crit/ t=val:val_warn:warn/' "$work/rrd/testhost/.fileset-index" \
	>"$work/fsidx.tmp" && mv "$work/fsidx.tmp" "$work/rrd/testhost/.fileset-index"
render_thr
grep -aq "Content-type: image/png" "$work/out" || fail "corrupt-relation graph does not render"
grep -aq "FFCC00" "$work/out" && fail "corrupt relation must not be threshold-styled"
[ "$(grep -ac 'DEF:v[0-9]' "$work/out")" = "2" ] \
	|| fail "corrupt relation suppressed a dataset from the peer plot: $(grep -a DEF: "$work/out" | head -3)"

# A block instance's file is created on the first sample and the graph
# renders from it normally (an unknown banner attribute is ignored).
ts=$(date +%s)
{
	printf '@@status|%s|127.0.0.1|origin|testhost|diskio|%s|green||green|%s|0||0||%s|0|linux|/\n' \
		"$ts" $((ts+1800)) "$ts" "$ts"
	printf '<!--XYMON METRICS: flatset probe=1\nDS:v:GAUGE:600:0:U\nidle 42\n-->\ns\n@@\n'
} | env XYMONHOME="$work" XYMONTMP="$work/tmp" \
	"$XYMOND_RRD" --rrddir="$work/rrd" --no-cache 2>/dev/null
[ -f "$rrds/flatset.idle.rrd" ] || fail "first sample must create the file (unknown attribute ignored)"
REQUEST_METHOD=GET \
QUERY_STRING="host=testhost&service=flatset&graph=hourly&action=view" \
XYMONHOME="$work" XYMONRRDS="$work/rrd" \
	"$work/showgraph" --debug --config="$work/graphs.cfg" \
	--rrddir="$rrds" >"$work/out" 2>&1 || true
grep -aq "DEF:.*flatset.idle.rrd" "$work/out" || fail "created instance does not graph: $(grep -a 'DEF\|No RRD' "$work/out" | head -3)"
grep -aq "Content-type: image/png" "$work/out" \
	|| fail "graph does not render: $(grep -a 'ERROR\|error' "$work/out" | head -3)"

# Disk legend end-to-end: the stock [disk] FNPATTERN ("^disk(.*).rrd")
# captures the "." separator of encoded files, and the decode path must
# absorb it - a migrated disk.%2Fvar.rrd must legend as "/var", never
# "./var"; root (disk.%2F.rrd) as "/"; a not-yet-migrated legacy
# disk,olddisk.rrd keeps the comma->slash legend "/olddisk".
cp "$rrds/diskio_ops.ada0.rrd" "$rrds/disk.%2Fvar.rrd"
cp "$rrds/diskio_ops.ada0.rrd" "$rrds/disk.%2F.rrd"
cp "$rrds/diskio_ops.ada0.rrd" "$rrds/disk,olddisk.rrd"
render "disk"
grep -aq '\./var' "$work/out" && fail "encoded disk legend shows './var' - separator not absorbed"
grep -aq ':/var' "$work/out" || fail "encoded disk legend '/var' missing: $(grep -a 'disk' "$work/out" | head -3)"
grep -aq ':/ ' "$work/out" || fail "root disk legend '/' missing: $(grep -a 'disk' "$work/out" | head -3)"
grep -aq ':/olddisk' "$work/out" || fail "legacy comma-encoded disk legend '/olddisk' missing"

# EXSTALEPATTERN: with nostale, files older than the fixed window are
# filtered - except the instances the graph's pattern exempts.
ts=$(date +%s)
{
	printf '@@status|%s|127.0.0.1|origin|testhost|diskio|%s|green||green|%s|0||0||%s|0|linux|/\n' \
		"$ts" $((ts+1800)) "$ts" "$ts"
	printf '<!--XYMON METRICS: stx\nDS:v:GAUGE:600:0:U\nkeep 1\ngone 2\n-->\ns\n@@\n'
} | env XYMONHOME="$work" XYMONTMP="$work/tmp" \
	"$XYMOND_RRD" --rrddir="$work/rrd" --no-cache 2>/dev/null
printf '[stx]\n\tEXSTALEPATTERN keep\n' >>"$work/graphs.cfg"
touch -d '3 days ago' "$rrds/stx.keep.rrd" "$rrds/stx.gone.rrd"
REQUEST_METHOD=GET \
QUERY_STRING="host=testhost&service=stx&graph=hourly&action=view&nostale=on" \
XYMONHOME="$work" XYMONRRDS="$work/rrd" \
	"$work/showgraph" --debug --config="$work/graphs.cfg" \
	--rrddir="$rrds" >"$work/out" 2>&1 || true
grep -aq "stx.keep.rrd" "$work/out" || fail "EXSTALEPATTERN-exempt stale instance was filtered"
grep -aq "stx.gone.rrd" "$work/out" && fail "non-exempt stale instance was not filtered"

pass "showgraph synthesizes a working gdef for marker-created RRD files"
