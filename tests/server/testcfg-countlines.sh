#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/testcfg-countlines.sh
#
# A test.cfg COUNTLINES metric joins the line-counting (--multigraphs) set:
# the status page derives its graph-paging linecount from the body lines for
# a column absent from the built-in list. A column without COUNTLINES keeps
# linecount 0. Drives the real generate_html_log().

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

work=$(mktemp -d "${TMPDIR:-/tmp}/xymon-tccount.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM

[ -f "$ROOT/include/config.h" ] && [ -f "$ROOT/lib/libxymoncomm.a" ] \
	|| skip "tree not built (run make first; the post-build CI suite covers this)"
[ -w "$ROOT/lib" ] || skip "source tree not writable (cannot refresh libxymoncomm.a)"
make -C "$ROOT/lib" libxymoncomm.a >"$work/libbuild.log" 2>&1 \
	|| { cat "$work/libbuild.log" >&2; fail "cannot refresh libxymoncomm.a"; }

"$CC" -I"$ROOT/include" -I"$ROOT/lib" -o "$work/harness" \
	"$here/testcfg-countlines-harness.c" "$ROOT/lib/libxymoncomm.a" \
	$pcre_libs -lssl -lcrypto 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; fail "harness does not compile"; }

mkdir -p "$work/etc"
cat >"$work/etc/test.cfg" <<'EOF'
TEST clx {
        SOURCE client
        METRIC clx { COUNTLINES }
}
EOF

# clx and plain both have a graph (self-mapped, listed in GRAPHS), neither is
# in the built-in multigraphs list. Only clx has COUNTLINES via test.cfg.
XYMONHOME="$work" \
CGIBINURL="/xymon-cgi" \
RRDWIDTH=576 RRDHEIGHT=120 \
XYMONSKIN="/xymon/gifs" XYMONWEB="/xymon" IMAGEFILETYPE="gif" \
TEST2RRD="plain" \
GRAPHS="clx,plain" \
INFOCOLUMN="info" TRENDSCOLUMN="trends" ACKUNTILMSG="until %H:%M" \
	"$work/harness" 2>"$work/stderr.log" || fail "countlines assertions failed: $(cat "$work/stderr.log")"

pass "test.cfg COUNTLINES joins the line-counting set for graph paging"
