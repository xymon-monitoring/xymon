#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/web/showgraph-env-after-rrd.sh
#
# rrd_graph starts a thread that reads the environment while it runs: Pango's
# "[pango] FcInit", in getenv(). xgetenv() writes a variable's built-in
# default into the environment the first time it is asked for one that is
# unset, and that setenv() can move the environment under the thread's
# getenv(): the thread crashes in getenv() and the CGI dies on SIGSEGV. A
# failed graph's error page asked for HTMLCONTENTTYPE only then, and an
# environment without it -- a site whose xymonserver.cfg lacks the line,
# showgraph run outside cgiwrap -- lost one failed graph now and then to the
# crash. showgraph-rrd-error-exit.sh saw it on a loaded machine.
#
# The crash is a race. What makes it possible is not: an environment write
# once rrd_graph has started. showgraph is linked here with rrd_graph, setenv
# and putenv wrapped (ld --wrap), and every setenv or putenv after rrd_graph
# has started is reported; a failed graph and a drawn one, both with no
# HTMLCONTENTTYPE in the environment, must report none.
#
# Skipped where the linker has no --wrap (macOS's ld64).

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

# The wrappers. rrd_graph is declared with plain pointers rather than through
# rrd.h, whose prototype differs between rrdtool versions; every argument is a
# pointer or an int, so the call is the same either way.
cat >"$work/envwatch.c" <<'EOF'
#include <stdio.h>
#include <stdlib.h>

static int rrd_started = 0;

int __real_rrd_graph(int, void *, void *, void *, void *, void *, void *, void *);
int __wrap_rrd_graph(int argc, void *argv, void *calcpr, void *xsize, void *ysize,
		     void *prdata, void *ymin, void *ymax)
{
	rrd_started = 1;
	return __real_rrd_graph(argc, argv, calcpr, xsize, ysize, prdata, ymin, ymax);
}

int __real_setenv(const char *, const char *, int);
int __wrap_setenv(const char *name, const char *value, int overwrite)
{
	if (rrd_started) fprintf(stderr, "ENVWRITE-AFTER-RRD setenv %s\n", name);
	return __real_setenv(name, value, overwrite);
}

int __real_putenv(char *);
int __wrap_putenv(char *s)
{
	if (rrd_started) fprintf(stderr, "ENVWRITE-AFTER-RRD putenv %s\n", s);
	return __real_putenv(s);
}
EOF

# The linker must know --wrap
printf 'int main(void) { return 0; }\n' >"$work/probe.c"
"$CC" -o "$work/probe" "$work/probe.c" -Wl,--wrap=puts >/dev/null 2>&1 \
	|| skip "the linker has no --wrap (needed to watch the environment writes)"

build_xymon_libs "$ROOT" "$work/libbuild.log" libxymoncomm.a

harness_cflags=$(xymon_cflags "$ROOT")
harness_ldflags=$(xymon_ldflags "$ROOT")
# shellcheck disable=SC2086  # the flag lists are meant to split
"$CC" $harness_cflags $rrddef $rrdinc -o "$work/showgraph" \
	"$ROOT/web/showgraph.c" "$work/envwatch.c" "$(xymon_lib "$ROOT" libxymoncomm.a)" \
	$pcre_libs $rrdlibs $harness_ldflags \
	-Wl,--wrap=rrd_graph -Wl,--wrap=setenv -Wl,--wrap=putenv 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; fail "showgraph does not compile with the environment watch"; }

cp "$ROOT/xymond/etcfiles/graphs.cfg.DIST" "$work/graphs.cfg"

# graph DIR -- request the cpu graph of host alpha from DIR, with an
# environment that has no HTMLCONTENTTYPE; prints what the watch reported
graph() {
	env -i PATH=/usr/bin:/bin REQUEST_METHOD=GET \
		QUERY_STRING="host=alpha&service=cpu&graph=hourly&action=view" \
		XYMONHOME="$work" TEST2RRD="cpu=la" \
		"$work/showgraph" --config="$work/graphs.cfg" --rrddir="$1" \
		>"$work/page" 2>"$work/stderr" || true
	grep 'ENVWRITE-AFTER-RRD' "$work/stderr" || true
}

# A failed graph: an empty stub la.rrd, which rrd_graph cannot map
mkdir -p "$work/fail"
: >"$work/fail/la.rrd"
writes=$(graph "$work/fail")
grep -q "<html><head><title>Invalid request</title></head>" "$work/page" \
	|| fail "the failed graph did not reach its error page: $(cat "$work/stderr")"
[ -z "$writes" ] || fail "a failed graph wrote to the environment after rrd_graph started, which its font thread may be reading: $writes"

# A drawn graph, from an RRD file librrd creates: rrd_create is declared with
# plain pointers, as rrd_graph above, rather than through rrd.h.
cat >"$work/mkrrd.c" <<'CEOF'
int rrd_create(int, char **);
int main(int argc, char **argv)
{
	char *args[] = { "create", argv[1], "--step", "300", "DS:la:GAUGE:600:0:U",
			 "RRA:AVERAGE:0.5:1:600", 0 };
	return (argc == 2) ? (rrd_create(6, args) != 0) : 2;
}
CEOF
# shellcheck disable=SC2086  # the flag lists are meant to split
"$CC" $harness_cflags -o "$work/mkrrd" "$work/mkrrd.c" $rrdlibs $harness_ldflags 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; fail "the RRD-creating helper does not compile"; }
mkdir -p "$work/ok"
"$work/mkrrd" "$work/ok/la.rrd" || fail "librrd could not create an RRD file"
writes=$(graph "$work/ok")
grep -q '^Content-type: image/png' "$work/page" \
	|| fail "the drawn graph did not come back as an image: $(cat "$work/stderr")"
[ -z "$writes" ] || fail "a drawn graph wrote to the environment after rrd_graph started: $writes"

pass "showgraph writes nothing to the environment once rrd_graph has started, for a failed graph or a drawn one"
