#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/fsidx-write-scoping.sh
#
# Write economics of the fileset index: a file's freshness is its own
# mtime, so plain data updates never write the index. Only index-only
# state does - schema declarations flush immediately, and a plain new
# file only joins an index that already exists. A host with no
# self-describing state never materializes an index file at all, and the
# readers (fsidx_count_*, the seed load) take file freshness from the
# mtime instead of the persisted ts.

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

work=$(mktemp -d "${TMPDIR:-/tmp}/xymon-fsidxscope.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM

[ -f "$ROOT/include/config.h" ] && [ -f "$ROOT/lib/libxymoncomm.a" ] \
	|| skip "tree not built (run make first; the post-build CI suite covers this)"

"$CC" -I"$ROOT/include" -I"$ROOT/lib" -o "$work/harness" \
	"$here/fsidx-scoping-harness.c" "$ROOT/lib/libxymoncomm.a" \
	$pcre_libs -lssl -lcrypto 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; fail "harness does not compile"; }

run() {  # run <scenario>
	"$work/harness" "$work/rrd" "$1"
}
fresh() {
	rm -rf "$work/rrd"; mkdir -p "$work/rrd/h1"
}
export XYMONRRDS="$work/rrd"

# A pure-legacy host: three cycles of data updates and flushes,
# including the shutdown flush - no index file may ever materialize.
fresh
out=$(run legacy)
echo "$out" | grep -q 'index=absent' || fail "legacy host materialized an index: $out"

# A declared entry creates the index exactly once. Plain commits never
# rewrite it (same inode across cycles); a new plain file joining the
# host does rewrite it, so the index stays complete.
fresh
out=$(run schema-once)
echo "$out" | grep -q 'created=present' || fail "declaration did not materialize the index: $out"
ino1=$(echo "$out" | sed -n 's/^created=present ino:\([0-9]*\)$/\1/p')
ino2=$(echo "$out" | sed -n 's/^aftercommits=present ino:\([0-9]*\)$/\1/p')
ino3=$(echo "$out" | sed -n 's/^afteradd=present ino:\([0-9]*\)$/\1/p')
[ -n "$ino1" ] && [ -n "$ino2" ] && [ -n "$ino3" ] || fail "missing stat lines: $out"
[ "$ino1" = "$ino2" ] || fail "plain commits rewrote the index (ino $ino1 -> $ino2): $out"
[ "$ino2" != "$ino3" ] || fail "a new file did not join the existing index: $out"
echo "$out" | grep -q 'f\.c\.rrd' || fail "new file missing from the index: $out"
echo "$out" | grep -q 'u=v:pct' || fail "declared units lost: $out"

# Reader freshness. The crafted index says everything was last written
# at ts=1000 (ancient); the REAL disk files are touched now. Files must
# count by mtime, and a record whose file is gone must age out.
fresh
touch "$work/rrd/h1/disk.%2Fvar.rrd" "$work/rrd/h1/disk.%2Ftmp.rrd"
{
	echo "# xymon fileset index v1"
	echo "disk.%2Fvar.rrd 1000"
	echo "disk.%2Ftmp.rrd 1000"
	echo "gone.a.rrd 1000"
} >"$work/rrd/h1/.fileset-index"
out=$(run count)
echo "$out" | grep -q 'disk=2' || fail "files not counted by mtime: $out"
echo "$out" | grep -q 'gone=0' || fail "a gone file's record did not age out: $out"

# Writer seed: a restart loads the index with its stale real-file ts;
# the census (AGGDS warm-up) must see the file's mtime instead.
fresh
touch "$work/rrd/h1/disk.%2Fvar.rrd"
{
	echo "# xymon fileset index v1"
	echo "disk.%2Fvar.rrd 1000"
} >"$work/rrd/h1/.fileset-index"
out=$(run census)
echo "$out" | grep -q 'fresh=2' || fail "seed did not derive real-file freshness from mtime: $out"

echo "OK $(basename "$0")"
