#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/xtree-iterate-deleted.sh
#
# Iterating a tree after xtreeDelete must never visit a deleted record.
# The fallback (array) variant only tombstones on delete: xtreeFirst used
# to return slot 0 without checking the tombstone flag, handing back
# userdata the caller had already freed - xymond_rrd's update-cache
# eviction hit this on every full-tree walk once the first-sorting entry
# was evicted. xtreeNext also read one slot past the array when the
# trailing entries were tombstones. Compile lib/tree.c BOTH ways - a
# config.h shim per variant, same scheme as xtree-destroy.sh - and walk
# a tree with deleted head/middle/tail entries under ASan, dereferencing
# every visited record so a stale pointer trips the sanitizer.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)

CC=${CC:-cc}
command -v "$CC" >/dev/null 2>&1 || skip "no C compiler available (CC=$CC)"

work=$(mktemp -d "${TMPDIR:-/tmp}/xymon-xtree-iter.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM

# ASan catches the use-after-free/overread; skip where unsupported.
echo 'int main(void){return 0;}' >"$work/probe.c"
"$CC" -fsanitize=address -o "$work/probe" "$work/probe.c" 2>/dev/null \
	&& "$work/probe" 2>/dev/null \
	|| skip "cc does not support -fsanitize=address"

cat >"$work/harness.c" <<'EOF'
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"	/* selects the tree variant - MUST match tree.c's */
#include "tree.h"

#define N 100

static int deleted_idx(int i)
{
	/* head, a middle run, and the tail - the tail run is what made the
	 * old fallback xtreeNext read past the end of the entry array */
	return (i == 0) || ((i >= 10) && (i < 20)) || (i >= N-3);
}

int main(void)
{
	char *keys[N]; int *vals[N];
	void *tree = xtreeNew(strcmp);
	xtreePos_t pos;
	int i, seen = 0, ndeleted = 0;

	for (i = 0; i < N; i++) {
		char buf[32];
		sprintf(buf, "key%04d", i);
		keys[i] = strdup(buf);
		vals[i] = malloc(sizeof(int)); *vals[i] = i;
		if (xtreeAdd(tree, keys[i], vals[i]) != XTREE_STATUS_OK) return 10;
	}

	/* Delete and free like the updcache eviction does: the record's
	 * userdata (and the caller's key copy) die with the delete. */
	for (i = 0; i < N; i++) {
		if (!deleted_idx(i)) continue;
		if (xtreeDelete(tree, keys[i]) != vals[i]) return 11;
		free(vals[i]); vals[i] = NULL;
		free(keys[i]); keys[i] = NULL;
		ndeleted++;
	}

	for (pos = xtreeFirst(tree); (pos != xtreeEnd(tree)); pos = xtreeNext(tree, pos)) {
		int *v = (int *)xtreeData(tree, pos);
		if (deleted_idx(*v)) return 12;	/* visited a deleted record (ASan usually fires first) */
		seen++;
	}
	if (seen != N - ndeleted) return 13;

	xtreeDestroy(tree);
	for (i = 0; i < N; i++) { free(keys[i]); free(vals[i]); }

	/* All-deleted tree: xtreeFirst must return end, not slot 0 */
	{
		char *k[3]; int *v[3];
		tree = xtreeNew(strcmp);
		for (i = 0; i < 3; i++) {
			char buf[8];
			sprintf(buf, "k%d", i);
			k[i] = strdup(buf);
			v[i] = malloc(sizeof(int)); *v[i] = i;
			if (xtreeAdd(tree, k[i], v[i]) != XTREE_STATUS_OK) return 20;
		}
		for (i = 0; i < 3; i++) {
			if (xtreeDelete(tree, k[i]) != v[i]) return 21;
			free(v[i]); free(k[i]);
		}
		if (xtreeFirst(tree) != xtreeEnd(tree)) return 22;
		xtreeDestroy(tree);
	}

	return 0;
}
EOF

# One config.h shim per variant, never the build's real config.h - each
# compile is guaranteed to be the variant it claims.
mkdir -p "$work/shim-tsearch" "$work/shim-fallback"
echo '#define HAVE_BINARY_TREE 1' >"$work/shim-tsearch/config.h"
echo '#undef HAVE_BINARY_TREE' >"$work/shim-fallback/config.h"

"$CC" -g -fsanitize=address -I"$work/shim-tsearch" -I"$ROOT/lib" \
	-o "$work/t-tsearch" "$work/harness.c" "$ROOT/lib/tree.c" 2>"$work/cc1.log" \
	|| { cat "$work/cc1.log" >&2; fail "tsearch-variant harness does not compile"; }
"$work/t-tsearch" >"$work/out1" 2>&1 \
	|| fail "tsearch variant visits deleted records (rc=$?): $(tail -15 "$work/out1")"

"$CC" -g -fsanitize=address -I"$work/shim-fallback" -I"$ROOT/lib" \
	-o "$work/t-fallback" "$work/harness.c" "$ROOT/lib/tree.c" 2>"$work/cc2.log" \
	|| { cat "$work/cc2.log" >&2; fail "fallback-variant harness does not compile"; }
"$work/t-fallback" >"$work/out2" 2>&1 \
	|| fail "fallback variant visits deleted records (rc=$?): $(tail -15 "$work/out2")"

echo "OK $(basename "$0")"
