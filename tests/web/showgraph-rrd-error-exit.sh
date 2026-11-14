#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/web/showgraph-rrd-error-exit.sh
#
# When rrd_graph fails, showgraph answers with an error page and ends. With
# Pango 1.50 and later, rrd_graph has by then started a thread that loads
# the fonts ("[pango] FcInit"), and ending through exit() tears the process
# down under that thread: it crashes in libc, and the CGI dies on a signal
# instead of sending its error page, on a few percent of failed graphs.
#
# The crash is a race, so one request proves nothing: the test sends the
# same failing request many times, in parallel, and checks that every one
# ends with status 1 and the error page, none on a signal. Where the race
# cannot happen (an older Pango) or is too rare to show, the test passes
# without having seen it; a correct showgraph never fails it.
#
# The failure is an empty stub la.rrd: the file exists, so showgraph hands
# it to rrd_graph, which cannot map it.

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

rrds="$work/rrd/alpha"
mkdir -p "$rrds"
: > "$rrds/la.rrd"
cp "$ROOT/xymond/etcfiles/graphs.cfg.DIST" "$work/graphs.cfg"

# One request: its exit status into $work/rc.N, its page into $work/out.N
cat > "$work/one.sh" <<EOF
#!/bin/sh
REQUEST_METHOD=GET QUERY_STRING="host=alpha&service=cpu&graph=hourly&action=view" \\
XYMONHOME="$work" TEST2RRD="cpu=la" \\
	"$work/showgraph" --config="$work/graphs.cfg" --rrddir="$rrds" \\
	> "$work/out.\$1" 2>/dev/null
echo \$? > "$work/rc.\$1"
EOF
chmod +x "$work/one.sh"

runs=200
jobs=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)
seq 1 "$runs" | xargs -P "$jobs" -n 1 "$work/one.sh"

signalled=0
for n in $(seq 1 "$runs"); do
	rc=$(cat "$work/rc.$n")
	if [ "$rc" -gt 128 ]; then
		signalled=$((signalled + 1))
		continue
	fi
	assert_equal "1" "$rc" "request $n: a failed graph ends with status 1"
	grep -q "<html><head><title>Invalid request</title></head>" "$work/out.$n" \
		|| fail "request $n: no error page"
done
assert_equal "0" "$signalled" "of $runs failed graphs, $signalled ended on a signal instead of the error page"

pass "a graph rrd_graph cannot draw ends with the error page, never on a signal ($runs requests)"
