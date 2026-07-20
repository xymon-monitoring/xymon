#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/web/html-log-markers.sh
#
# Self-describing statuses: XYMON GRAPH markers declare the graphs a status
# page shows, with per-graph paging counts (derived from the message's own
# METRICS block, instances=N, or instances=all). Drives the real generate_html_log()
# through a small C harness; see the harness for the full assertion list.

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

work=$(mktemp -d "${TMPDIR:-/tmp}/xymon-htmllog-markers.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM

# Split sizes declared in the graph definition itself (MAXINSTANCESPERIMAGE); the
# diskio_split entry also has a legacy ::4 in GRAPHS - MAXINSTANCESPERIMAGE wins.
mkdir -p "$work/etc"
cat >"$work/etc/graphs.cfg" <<'GDEFS'
[diskio_ops]
	MAXINSTANCESPERIMAGE 1
	TITLE Disk operations
[diskio_split]
	MAXINSTANCESPERIMAGE 2
[diskio_gzy]
	STOREPATTERN .
	FNPATTERN ^gzyfiles\..+\.rrd
[diskio_idx]
	STOREPATTERN .
[diskio_filt]
	EXSTOREPATTERN x
[diskio_slow]
	STOREPATTERN .
	FNPATTERN ^diskio_idx\..+\.rrd
	EXSTALEPATTERN old
GDEFS

# The harness links libxymoncomm; a never-built tree skips (the post-build
# CI suite exercises it), a built tree refreshes the archive incrementally.
[ -f "$ROOT/include/config.h" ] && [ -f "$ROOT/lib/libxymoncomm.a" ] \
	|| skip "tree not built (run make first; the post-build CI suite covers this)"
[ -w "$ROOT/lib" ] || skip "source tree not writable (cannot refresh libxymoncomm.a)"
make -C "$ROOT/lib" libxymoncomm.a >"$work/libbuild.log" 2>&1 \
	|| { cat "$work/libbuild.log" >&2; fail "cannot refresh libxymoncomm.a"; }

"$CC" -I"$ROOT/include" -I"$ROOT/lib" -o "$work/harness" \
	"$here/html-log-markers-harness.c" "$ROOT/lib/libxymoncomm.a" \
	$pcre_libs -lssl -lcrypto 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; fail "harness does not compile"; }

# A writer-kept fileset index for the store-filtered diskio_idx graph:
# three fresh entries and one stale one (the staleness cutoff must
# exclude it).
mkdir -p "$work/rrd/testhost"
now=$(date +%s)
{
	echo "# xymon fileset index v1"
	echo "diskio_idx.a.rrd $now"
	echo "diskio_idx.b.rrd $now"
	echo "diskio_idx.c.rrd $now"
	echo "diskio_idx.old.rrd $((now - 200000))"
	echo "gzyfiles.p.rrd $now"
	echo "gzyfiles.q.rrd $now"
	echo "gzyfiles.stale.rrd $((now - 200000))"
} >"$work/rrd/testhost/.fileset-index"

XYMONHOME="$work" \
XYMONRRDS="$work/rrd" \
CGIBINURL="/xymon-cgi" \
RRDWIDTH=576 \
RRDHEIGHT=120 \
XYMONSKIN="/xymon/gifs" \
XYMONWEB="/xymon" \
IMAGEFILETYPE="gif" \
TEST2RRD="cpu=la,disk,if_load=devmon" \
GRAPHS="la,disk,tcp,devmon,diskio_busy::2,diskio_split::4" \
GRAPHS_smart="smart-temp" \
GRAPHS_gzycol="diskio_gzy" \
INFOCOLUMN="info" \
TRENDSCOLUMN="trends" \
ACKUNTILMSG="until %H:%M" \
	"$work/harness" 2>"$work/stderr.log" || fail "harness assertions failed: $(cat "$work/stderr.log")"

pass "XYMON GRAPH markers render with per-graph paging counts"
