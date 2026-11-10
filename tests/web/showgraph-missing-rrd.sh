#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/web/showgraph-missing-rrd.sh
#
# rrdtool fails a whole graph on the first RRD file that is missing, and the
# metrics report asks for graphs a host may have no data for. showgraph
# answers with the data there is:
#
# - several hosts (host=a,b,c): each host's file comes from the graph's
#   -multi definition in graphs.cfg; a host without it is left out, and the
#   others are drawn;
# - one host, with a fixed definition: the files are those its DEF: lines
#   name ([ram] draws from memory.real.rrd, not ram.rrd); with one missing,
#   the graph is empty;
# - an empty graph is the blank image, as for a pattern that matches nothing.
#
# Drives the real showgraph CGI with --debug against a directory of empty
# stub RRD files and checks the files named in the rrd_graph argument dump.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)

CC=${CC:-cc}
command -v "$CC" >/dev/null 2>&1 || skip "no C compiler available (CC=$CC)"
require_gnu_make

if [ ! -f "$ROOT/include/config.h" ] || [ ! -f "$(xymon_lib "$ROOT" libxymoncomm.a)" ]; then
	skip "tree not built (run make first; the post-build CI suite covers this)"
fi
[ -f "$ROOT/web/showgraph.cgi" ] || skip "tree built without RRD support (no showgraph.cgi)"

rrddef=$(xymon_makevar "$ROOT" RRDDEF)
rrdinc=$(xymon_makevar "$ROOT" RRDINCDIR)
rrdlibs=$(xymon_makevar "$ROOT" RRDLIBS)
[ -n "$rrdlibs" ] || rrdlibs="-lrrd"

pcre_libs=${PCRELIBS:-}
if [ -z "$pcre_libs" ] && command -v pkg-config >/dev/null 2>&1; then
	pcre_libs=$(pkg-config --libs libpcre2-8 2>/dev/null || true)
fi
[ -n "$pcre_libs" ] || pcre_libs="-lpcre2-8"

work=$(mktempdir)

build_xymon_libs "$ROOT" "$work/libbuild.log" libxymoncomm.a

harness_cflags=$(xymon_cflags "$ROOT")
harness_ldflags=$(xymon_ldflags "$ROOT")
# shellcheck disable=SC2086  # the flag lists are meant to split
"$CC" $harness_cflags $rrddef $rrdinc -o "$work/showgraph" \
	"$ROOT/web/showgraph.c" "$(xymon_lib "$ROOT" libxymoncomm.a)" \
	$pcre_libs $rrdlibs $harness_ldflags 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; fail "showgraph does not compile"; }

# alpha and beta have the cpu graph's file ([la-multi] FNPATTERN la.rrd);
# gamma has only its ping data, as a host without a client
rrds="$work/rrd"
mkdir -p "$rrds/alpha" "$rrds/beta" "$rrds/gamma" "$work/run"
touch "$rrds/alpha/la.rrd" "$rrds/beta/la.rrd" "$rrds/gamma/tcp.conn.rrd"

# The stock graphs.cfg, and the stock cpu=la mapping
cp "$ROOT/xymond/etcfiles/graphs.cfg.DIST" "$work/graphs.cfg"

# render HOSTS SERVICE [RRDDIR] -- one image request; rrd_graph fails on the
# stub files, but the --debug argument dump comes out before it. RRDDIR is
# the directory of all hosts for several, the host's own for one.
render() {
	REQUEST_METHOD=GET \
	QUERY_STRING="host=$1&service=$2&graph=hourly&action=view" \
	XYMONHOME="$work" XYMONRUNDIR="$work/run" TEST2RRD="cpu=la,conn=tcp" \
		"$work/showgraph" --debug --config="$work/graphs.cfg" \
		--rrddir="${3:-$rrds}" 2>&1 | tr -d '\000' || true
}

# A host without the file is left out; the others are still drawn
out=$(render "alpha,gamma,beta" cpu)
assert_contains     "=alpha/la.rrd:"  "$out" "a host with the file is drawn"
assert_contains     "=beta/la.rrd:"   "$out" "a host after the missing one is drawn"
assert_not_contains "=gamma/la.rrd:"  "$out" "a host without the file is left out"
assert_contains     "gamma has no la.rrd" "$out" "the left-out host is named in the debug output"

# The same hosts on a graph all three have ([conn-multi]): nobody is left out
out=$(render "alpha,gamma,beta" conn-multi)
assert_contains     "=gamma/tcp.conn.rrd:" "$out" "a host with this graph's file is kept"

# No host has the file: the blank image, not a graph of nothing. Two hosts,
# because one alone is drawn as a single host's graph.
out=$(render "gamma,delta" cpu)
assert_not_contains "/la.rrd:"        "$out" "no file is named when no host has it"
assert_contains     "Content-type: image/png" "$out" "the answer is still an image"

# One host: a fixed graph it has the file for is drawn ([la], via cpu=la)
out=$(render alpha cpu "$rrds/alpha")
assert_contains     "=la.rrd:"         "$out" "one host: a graph it has the file for is drawn"

# One host: [ram] names memory.real.rrd in its DEF:, which alpha lacks
out=$(render alpha ram "$rrds/alpha")
assert_not_contains "memory.real.rrd:" "$out" "one host: a graph whose DEF: file is missing is not drawn"
assert_contains     "Content-type: image/png" "$out" "one host: the empty graph is still an image"

pass "showgraph draws a graph from the hosts and files that exist, and an empty graph as the blank image"
