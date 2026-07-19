#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/xtree-destroy.sh
#
# xtreeDestroy must release everything the tree allocated. The tsearch
# variant (HAVE_BINARY_TREE) used to free only the handle, leaking every
# internal node and record wrapper of the destroyed tree - the fileset
# index hits this on every drophost. Compile lib/tree.c BOTH ways - a
# config.h shim per variant, so the test needs no built tree and is
# independent of this platform's configure result - and run an
# insert/find/delete/destroy workload under ASan's leak checker.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)

CC=${CC:-cc}
command -v "$CC" >/dev/null 2>&1 || skip "no C compiler available (CC=$CC)"

work=$(mktemp -d "${TMPDIR:-/tmp}/xymon-xtree.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM

# ASan with leak detection is the whole point; skip where unsupported.
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

int main(void)
{
	char *keys[1000]; int *vals[1000];
	void *tree = xtreeNew(strcmp);
	xtreePos_t pos;
	int i, seen = 0;

	for (i = 0; i < 1000; i++) {
		char buf[32];
		sprintf(buf, "key%04d", i);
		keys[i] = strdup(buf);
		vals[i] = malloc(sizeof(int)); *vals[i] = i;
		if (xtreeAdd(tree, keys[i], vals[i]) != XTREE_STATUS_OK) return 10;
	}
	if (xtreeFind(tree, "key0500") == xtreeEnd(tree)) return 11;
	for (pos = xtreeFirst(tree); (pos != xtreeEnd(tree)); pos = xtreeNext(tree, pos)) seen++;
	if (seen != 1000) return 12;
	for (i = 0; i < 1000; i += 7) {
		if (xtreeDelete(tree, keys[i]) != vals[i]) return 13;
	}
	xtreeDestroy(tree);
	/* Keys and userdata stay caller-owned through delete AND destroy */
	for (i = 0; i < 1000; i++) { free(keys[i]); free(vals[i]); }

	/* Empty and NULL trees must be safe too */
	xtreeDestroy(xtreeNew(strcmp));
	xtreeDestroy(NULL);
	return 0;
}
EOF

# One config.h shim per variant: never the build's real config.h, so
# (a) the suite's no-build lane can run this test, and (b) each compile
# is guaranteed to be the variant it claims, whatever configure decided.
mkdir -p "$work/shim-tsearch" "$work/shim-fallback"
echo '#define HAVE_BINARY_TREE 1' >"$work/shim-tsearch/config.h"
echo '#undef HAVE_BINARY_TREE' >"$work/shim-fallback/config.h"

"$CC" -g -fsanitize=address -I"$work/shim-tsearch" -I"$ROOT/lib" \
	-o "$work/t-tsearch" "$work/harness.c" "$ROOT/lib/tree.c" 2>"$work/cc1.log" \
	|| { cat "$work/cc1.log" >&2; fail "tsearch-variant harness does not compile"; }
"$work/t-tsearch" >"$work/out1" 2>&1 \
	|| fail "tsearch variant leaks or fails (rc=$?): $(tail -15 "$work/out1")"

"$CC" -g -fsanitize=address -I"$work/shim-fallback" -I"$ROOT/lib" \
	-o "$work/t-fallback" "$work/harness.c" "$ROOT/lib/tree.c" 2>"$work/cc2.log" \
	|| { cat "$work/cc2.log" >&2; fail "fallback-variant harness does not compile"; }
"$work/t-fallback" >"$work/out2" 2>&1 \
	|| fail "fallback variant leaks or fails (rc=$?): $(tail -15 "$work/out2")"

echo "OK $(basename "$0")"
