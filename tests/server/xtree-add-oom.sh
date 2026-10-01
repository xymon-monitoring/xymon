#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/xtree-add-oom.sh
#
# xtreeAdd (fallback array variant) grew the array with realloc()/malloc()
# whose result it never checked. On failure realloc() returns NULL *and* leaves
# the old block allocated: the old code overwrote mytree->entries with that
# NULL, leaking the array and then dereferencing NULL at
# entries[treesz].key = key. It must instead report XTREE_STATUS_MEM_EXHAUSTED
# and leave the tree unchanged.
#
# tree.c is compiled to call wrapper allocators (via -Dmalloc=... etc.) that a
# flag can force to fail, so the out-of-memory branch is driven deterministically.
# The unfixed code crashes on the failing realloc, so a plain build catches the
# regression on every platform; an ASan leg additionally proves no leak.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)

CC=${CC:-cc}
command -v "$CC" >/dev/null 2>&1 || skip "no C compiler available (CC=$CC)"

work=$(mktemp -d "${TMPDIR:-/tmp}/xymon-xtree-oom.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM

# Fallback (array) variant only: it is the one that realloc()s the array.
echo '#undef HAVE_BINARY_TREE' >"$work/config.h"

cat >"$work/harness.c" <<'EOF'
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"
#include "tree.h"

/* tree.c is compiled with -Dmalloc=xt_malloc etc. so its allocations land
   here; a one-shot flag forces the next call of a kind to fail. This file is
   compiled WITHOUT the -D, so the wrappers reach the real allocators. */
int xt_fail_malloc = 0, xt_fail_realloc = 0, xt_fail_calloc = 0;
void *xt_malloc(size_t n)           { if (xt_fail_malloc)  { xt_fail_malloc = 0;  return NULL; } return malloc(n); }
void *xt_calloc(size_t a, size_t b) { if (xt_fail_calloc)  { xt_fail_calloc = 0;  return NULL; } return calloc(a, b); }
void *xt_realloc(void *p, size_t n) { if (xt_fail_realloc) { xt_fail_realloc = 0; return NULL; } return realloc(p, n); }

int main(void)
{
	void *tree = xtreeNew(strcmp);
	char *ka = strdup("a"), *kz = strdup("z"), *km = strdup("m");
	int va = 1, vz = 2, vm = 3;

	if (xtreeAdd(tree, ka, &va) != XTREE_STATUS_OK) return 10;	/* empty -> 1 */

	/* Append grows via realloc(): force it to fail. Must report, not crash. */
	xt_fail_realloc = 1;
	if (xtreeAdd(tree, kz, &vz) != XTREE_STATUS_MEM_EXHAUSTED) return 11;
	if (xtreeFind(tree, "a") == xtreeEnd(tree)) return 12;	/* unchanged */
	if (xtreeFind(tree, "z") != xtreeEnd(tree)) return 13;	/* not added */
	free(kz);						/* rejected key is the caller's */

	/* A real append then a middle insert (malloc()): fail that too. */
	kz = strdup("z");
	if (xtreeAdd(tree, kz, &vz) != XTREE_STATUS_OK) return 14;	/* [a,z] */
	xt_fail_malloc = 1;
	if (xtreeAdd(tree, km, &vm) != XTREE_STATUS_MEM_EXHAUSTED) return 15;
	if (xtreeFind(tree, "m") != xtreeEnd(tree)) return 16;	/* not added */
	if (xtreeFind(tree, "a") == xtreeEnd(tree)) return 17;	/* both survive */
	if (xtreeFind(tree, "z") == xtreeEnd(tree)) return 18;
	free(km);

	/* A normal add still works after the two rejections. */
	km = strdup("m");
	if (xtreeAdd(tree, km, &vm) != XTREE_STATUS_OK) return 19;

	xtreeDestroy(tree);
	free(ka); free(kz); free(km);
	return 0;
}
EOF

REDIR="-Dmalloc=xt_malloc -Dcalloc=xt_calloc -Drealloc=xt_realloc"

build_and_run() {  # build_and_run <label> [extra-cflags]
	local label=$1 extra=${2:-}
	# tree.c gets the allocator redirect; the harness does not (its wrappers
	# must reach the real allocators), so compile them separately, then link.
	# shellcheck disable=SC2086
	"$CC" -g $extra -I"$work" -iquote "$ROOT/lib" $REDIR \
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
		|| fail "$label: xtreeAdd out-of-memory path crashed or misbehaved (rc=$?): $(tail -15 "$work/out-$label")"
}

# Plain build first: the unfixed code NULL-derefs here, so this leg alone
# guards the regression, and it runs on platforms without a sanitizer.
build_and_run plain

# ASan leg (where available) additionally catches the leaked old array.
if asan_usable; then
	build_and_run asan "-fsanitize=address"
fi

echo "OK $(basename "$0")"
