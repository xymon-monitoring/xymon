#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/web/showgraph-errorpage-quoted.sh
#
# When rrd_graph fails, showgraph puts rrdtool's message on its error page,
# and that message quotes file names, which come from the graph definition
# and from the request. With action=selzoom the page is text/html, so the
# message is now passed through htmlquoted().
#
# The markup is in a file name the graph definition names (an empty stub
# "<i>X.rrd"): rrd_graph cannot map it, and its error names the file.

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

mkdir -p "$work/rrd/alpha"
: > "$work/rrd/alpha/<i>X.rrd"
cat > "$work/graphs.cfg" <<'EOF'
[markup]
	TITLE Markup
	YAXIS x
	DEF:p=<i>X.rrd:x:AVERAGE
	LINE2:p#0000FF:x
EOF

# The error page for the markup graph. Until a failed rrd_graph ends with
# _exit(), a run can die on a signal under Pango; such a run is retried.
render() {
	local i
	for i in 1 2 3 4 5; do
		if out=$(REQUEST_METHOD=GET \
			QUERY_STRING="host=alpha&service=markup&graph=hourly&action=selzoom" \
			XYMONHOME="$work" XYMONRUNDIR="$work/run" HTMLCONTENTTYPE="text/html" \
			"$work/showgraph" --config="$work/graphs.cfg" --rrddir="$work/rrd/alpha" 2>/dev/null) \
			|| [ $? -le 128 ]; then
			printf '%s' "$out"
			return
		fi
	done
	fail "showgraph died on a signal five times in a row"
}

out=$(render)
assert_contains     "Content-type: text/html" "$out" "the error page is HTML"
assert_contains     "&lt;i&gt;X.rrd"          "$out" "the file name in rrdtool's message is HTML-quoted"
assert_not_contains "<i>X.rrd"                "$out" "no markup from the file name reaches the page"

pass "showgraph HTML-quotes rrdtool's message on its error page"
