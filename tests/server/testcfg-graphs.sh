#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/testcfg-graphs.sh
#
# test.cfg's per-test GRAPHS override wins over the GRAPHS_<service>
# environment for the status-page graph list; a service with no test.cfg
# section falls back to the env. Drives the real generate_html_log().

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

work=$(mktemp -d "${TMPDIR:-/tmp}/xymon-tcgraphs.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM

[ -f "$ROOT/include/config.h" ] && [ -f "$ROOT/lib/libxymoncomm.a" ] \
	|| skip "tree not built (run make first; the post-build CI suite covers this)"
[ -w "$ROOT/lib" ] || skip "source tree not writable (cannot refresh libxymoncomm.a)"
make -C "$ROOT/lib" libxymoncomm.a >"$work/libbuild.log" 2>&1 \
	|| { cat "$work/libbuild.log" >&2; fail "cannot refresh libxymoncomm.a"; }

"$CC" -I"$ROOT/include" -I"$ROOT/lib" -o "$work/harness" \
	"$here/testcfg-graphs-harness.c" "$ROOT/lib/libxymoncomm.a" \
	$pcre_libs -lssl -lcrypto 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; fail "harness does not compile"; }

mkdir -p "$work/etc"
cat >"$work/etc/test.cfg" <<'EOF'
TEST smart {
        SOURCE client
        GRAPHS tc_temp, tc_status
        METRIC smart_temp
        METRIC smart_status
}
EOF

XYMONHOME="$work" \
CGIBINURL="/xymon-cgi" \
RRDWIDTH=576 RRDHEIGHT=120 \
XYMONSKIN="/xymon/gifs" XYMONWEB="/xymon" IMAGEFILETYPE="gif" \
TEST2RRD="" GRAPHS="" \
GRAPHS_smart="env_only" \
GRAPHS_legacy="env_graph" \
INFOCOLUMN="info" TRENDSCOLUMN="trends" ACKUNTILMSG="until %H:%M" \
	"$work/harness" 2>"$work/stderr.log" || fail "graphs override assertions failed: $(cat "$work/stderr.log")"

pass "test.cfg GRAPHS overrides GRAPHS_<service> env, with env fallback"
