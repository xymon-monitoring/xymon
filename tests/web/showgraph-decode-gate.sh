#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/web/showgraph-decode-gate.sh
#
# The instance-name decode must be evidence-gated, not shape-guessed:
#  - a legacy tcp.http file whose URL carries a literal %XX run must keep
#    the legacy rendering (comma->slash, no percent-decode),
#  - a file with a g=-declared fileset-index record (block writers only)
#    decodes even when escape-free, so a comma instance's legend does not
#    flip at the flat->file transition,
#  - the disk family always encodes since the cutover, so its captures
#    take the canonical-shape probe without needing an index record.
# Drives the real showgraph CGI with --debug and asserts on the rrd_graph
# argument dump.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)

CC=${CC:-cc}
command -v "$CC" >/dev/null 2>&1 || skip "no C compiler available (CC=$CC)"
command -v make >/dev/null 2>&1 || skip "make not available"

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

work=$(mktemp -d "${TMPDIR:-/tmp}/xymon-showgraph.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM

[ -w "$ROOT/lib" ] || skip "source tree not writable (cannot refresh libxymoncomm.a)"
make -C "$ROOT/lib" libxymoncomm.a >"$work/libbuild.log" 2>&1 \
	|| { cat "$work/libbuild.log" >&2; fail "cannot refresh libxymoncomm.a"; }

"$CC" -I"$ROOT/include" -I"$ROOT/lib" $rrddef -o "$work/showgraph" \
	"$ROOT/web/showgraph.c" "$ROOT/lib/libxymoncomm.a" \
	$pcre_libs $rrdlibs $ssllibs 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; fail "showgraph does not compile"; }

# Fake RRD directory; selection is by filename, empty stubs suffice
rrds="$work/rrd/testhost"
mkdir -p "$rrds"
touch "$rrds/tcp.http.httpbin.org,a%2Fb.rrd" \
	"$rrds/mysvc.eth0,1.rrd" \
	"$rrds/disk.%2Fvar.rrd"

# A g= declaration for the METRICS-written file only; the http and disk
# files carry plain freshness records like the daemon's dir-scan seeding
now=$(date +%s)
cat >"$rrds/.fileset-index" <<EOF
# xymon fileset index v1
mysvc.eth0,1.rrd $now u=v: d=v g=$now
tcp.http.httpbin.org,a%2Fb.rrd $now
disk.%2Fvar.rrd $now
EOF

# Stock graphs.cfg plus a custom gdef as a METRICS block would use
cp "$ROOT/xymond/etcfiles/graphs.cfg.DIST" "$work/graphs.cfg"
cat >>"$work/graphs.cfg" <<'EOF'

[mysvc]
	FNPATTERN ^mysvc\.(.+)\.rrd
	TITLE Custom metrics
	YAXIS v
	DEF:p@RRDIDX@=@RRDFN@:v:AVERAGE
	LINE2:p@RRDIDX@#@COLOR@:@RRDPARAM@
EOF

render() {
	REQUEST_METHOD=GET \
	QUERY_STRING="host=testhost&service=$1&graph=hourly&action=view" \
	XYMONHOME="$work" \
		"$work/showgraph" --debug --config="$work/graphs.cfg" \
		--rrddir="$rrds" 2>/dev/null || true
}

# Legacy http file with a literal %XX run: NOT decoded (the name merely
# looks like encoder output), legacy comma->slash still applies
out=$(render "tcp.http")
assert_contains     "httpbin.org/a%2Fb" "$out" "legacy http legend"
assert_not_contains "httpbin.org,a/b"   "$out" "legacy http legend"

# g=-declared escape-free instance: decoded (identity), comma preserved -
# the same legend the flat rendering produced before the file existed
out=$(render "mysvc")
assert_contains     "eth0,1" "$out" "declared comma instance"
assert_not_contains "eth0/1" "$out" "declared comma instance"

# Disk family: canonical-shape probe decodes without an index declaration.
# Only the legend (LINE2) is decoded - the DEF's filename keeps %2F, so
# the negative assertion is scoped to the legend line.
out=$(render "disk")
legend=$(printf '%s\n' "$out" | grep 'LINE2:p0' || true)
assert_contains     ":/var"  "$legend" "encoded disk legend"
assert_not_contains "%2Fvar" "$legend" "encoded disk legend"

pass "showgraph decodes instance names by evidence, not name shape"
