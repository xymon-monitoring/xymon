#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/xtree-tsearch-oom.sh
#
# The POSIX binary-tree (tsearch) xtree variant allocated its record wrapper and
# the tree handle with calloc() and never checked the result: xtreeAdd() did
# `rec = calloc(...); rec->key = key;` and xtreeNew() did `newtree = calloc(...);
# newtree->compare = ...`, both dereferencing NULL on out-of-memory. They must
# report instead - XTREE_STATUS_MEM_EXHAUSTED from xtreeAdd(), NULL from
# xtreeNew() - and leave the tree unchanged.
#
# tree.c is compiled with its calloc() redirected to a wrapper a flag can fail
# on demand (tsearch()'s own internal node allocation is libc malloc, untouched).
# The unfixed code NULL-derefs, so a plain build catches it everywhere; an ASan
# leg additionally proves nothing is leaked.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)

CC=${CC:-cc}
command -v "$CC" >/dev/null 2>&1 || skip "no C compiler available (CC=$CC)"

work=$(mktemp -d "${TMPDIR:-/tmp}/xymon-xtree-toom.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM

# tsearch variant. Skip where <search.h>'s POSIX tree API is absent - the same
# condition under which this variant is not built.
echo '#define HAVE_BINARY_TREE 1' >"$work/config.h"
cat >"$work/probe.c" <<'EOF'
#include <search.h>
#include <stdlib.h>
static int c(const void*a,const void*b){(void)a;(void)b;return 0;}
int main(void){void*r=NULL;int x=1;(void)tsearch(&x,&r,c);return 0;}
EOF
"$CC" -o "$work/probe" "$work/probe.c" 2>/dev/null || skip "POSIX binary-tree API (tsearch) not available"

cat >"$work/harness.c" <<'EOF'
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"
#include "tree.h"

/* tree.c is compiled with -Dcalloc=xt_calloc so its record/handle allocations
   land here; a one-shot flag fails the next one. This file is compiled without
   the -D, so the wrapper reaches the real calloc. */
int xt_fail_calloc = 0;
void *xt_calloc(size_t a, size_t b) { if (xt_fail_calloc) { xt_fail_calloc = 0; return NULL; } return calloc(a, b); }

int main(void)
{
	int va = 1, vb = 2, vc = 3;
	void *tree;

	/* xtreeNew() out of memory must return NULL, not dereference it. */
	xt_fail_calloc = 1;
	if (xtreeNew(strcmp) != NULL) return 20;

	tree = xtreeNew(strcmp);
	if (!tree) return 21;
	if (xtreeAdd(tree, "a", &va) != XTREE_STATUS_OK) return 10;

	/* xtreeAdd()'s record calloc out of memory must report, not crash. */
	xt_fail_calloc = 1;
	if (xtreeAdd(tree, "b", &vb) != XTREE_STATUS_MEM_EXHAUSTED) return 11;
	if (xtreeFind(tree, "a") == xtreeEnd(tree)) return 12;	/* unchanged */
	if (xtreeFind(tree, "b") != xtreeEnd(tree)) return 13;	/* not added */

	/* A normal add still works afterwards. */
	if (xtreeAdd(tree, "c", &vc) != XTREE_STATUS_OK) return 14;

	xtreeDestroy(tree);
	return 0;
}
EOF

build_and_run() {  # build_and_run <label> [extra-cflags]
	local label=$1 extra=${2:-}
	# shellcheck disable=SC2086
	"$CC" -g $extra -I"$work" -iquote "$ROOT/lib" -Dcalloc=xt_calloc \
		-c -o "$work/tree-$label.o" "$ROOT/lib/tree.c" 2>"$work/cc-$label.log" \
		|| { cat "$work/cc-$label.log" >&2; fail "$label: tree.c does not compile"; }
	# shellcheck disable=SC2086
	"$CC" -g $extra -I"$work" -iquote "$ROOT/lib" \
		-c -o "$work/harness-$label.o" "$work/harness.c" 2>>"$work/cc-$label.log" \
		|| { cat "$work/cc-$label.log" >&2; fail "$label: harness does not compile"; }
	# shellcheck disable=SC2086
	"$CC" -g $extra -o "$work/t-$label" "$work/harness-$label.o" "$work/tree-$label.o" 2>>"$work/cc-$label.log" \
		|| { cat "$work/cc-$label.log" >&2; fail "$label: link failed"; }
	"$work/t-$label" >"$work/out-$label" 2>&1 \
		|| fail "$label: tsearch xtreeAdd/xtreeNew out-of-memory path crashed or misbehaved (rc=$?): $(tail -15 "$work/out-$label")"
}

build_and_run plain
if asan_usable; then
	build_and_run asan "-fsanitize=address"
fi

echo "OK $(basename "$0")"
