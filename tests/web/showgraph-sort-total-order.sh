#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/web/showgraph-sort-total-order.sh
#
# rrd_name_compare() must be a total order. The old per-pair mode choice
# (numeric only when BOTH keys of a pair were numeric) was intransitive on
# mixed key sets (9 < 10 < "1a" yet "1a" < 9), which is undefined behaviour
# for qsort(3) - the sorted order depended on readdir() order. And distinct
# keys must never compare equal ("007" vs "7"), or their order is unspecified.
#
# This extracts the REAL comparator AND the real mode-selection function from
# web/showgraph.c and drives both, rather than replicating the pre-scan here: a
# replica keeps passing after production goes back to pairwise or lexical-only,
# which is precisely the regression this guards. It asserts that every input
# permutation of a mixed set sorts identically, and that numerically-equal
# distinct keys have one deterministic order.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
SRC="$ROOT/web/showgraph.c"
[ -f "$SRC" ] || skip "web/showgraph.c absent"

require_cc

work=$(mktempdir)

# Extract the comparator, its mode flag, and the mode-selection function from
# the real source. Both must be there: silently missing one would leave the
# driver testing nothing.
sed -n '/^static int rrd_keys_all_numeric/,/^}/p'  "$SRC" >  "$work/cmp.inc"
sed -n '/^static void rrd_set_sort_mode/,/^}/p'    "$SRC" >> "$work/cmp.inc"
grep -q "rrd_name_compare"   "$work/cmp.inc" || fail "could not extract rrd_name_compare from showgraph.c"
grep -q "rrd_set_sort_mode"  "$work/cmp.inc" || fail "could not extract rrd_set_sort_mode from showgraph.c"

cat > "$work/driver.c" <<'DRIVER'
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct rrddb_t { char *key; char *rrdfn; char *rrdparam; } rrddb_t;
#include "cmp.inc"
static void sortkeys(char **keys, int n, char *out) {
	rrddb_t r[8]; int i;
	for (i = 0; i < n; i++) { r[i].key = keys[i]; r[i].rrdfn = r[i].rrdparam = NULL; }
	rrd_set_sort_mode(r, n);
	qsort(r, n, sizeof(rrddb_t), rrd_name_compare);
	out[0] = '\0';
	for (i = 0; i < n; i++) { strcat(out, r[i].key); strcat(out, " "); }
}
int main(void) {
	/* mixed set: all 6 permutations must sort identically */
	char *perms[6][3] = { {"9","10","1a"}, {"9","1a","10"}, {"10","9","1a"},
			      {"10","1a","9"}, {"1a","9","10"}, {"1a","10","9"} };
	char first[64], got[64]; int i, fails = 0;
	sortkeys(perms[0], 3, first);
	for (i = 1; i < 6; i++) {
		sortkeys(perms[i], 3, got);
		if (strcmp(first, got) != 0) { printf("FAIL permutation %d: '%s' vs '%s'\n", i, got, first); fails++; }
	}
	if (!fails) printf("ok   mixed set: all 6 permutations sort as '%s'\n", first);
	/* all-numeric: numeric order, and 007 vs 7 deterministic (never equal) */
	{ char *k[3] = {"10","7","007"};
	  sortkeys(k, 3, got);
	  if (strcmp(got, "007 7 10 ") != 0) { printf("FAIL numeric: got '%s' want '007 7 10 '\n", got); fails++; }
	  else printf("ok   numeric set: '%s' (2-digit after 1-digit, 007 before 7)\n", got); }
	/* plain numeric ordering unchanged: 2 before 10 */
	{ char *k[2] = {"10","2"};
	  sortkeys(k, 2, got);
	  if (strcmp(got, "2 10 ") != 0) { printf("FAIL: got '%s' want '2 10 '\n", got); fails++; }
	  else printf("ok   plain integers: '%s'\n", got); }
	if (fails) { printf("%d check(s) FAILED\n", fails); return 1; }
	printf("all sort-total-order checks ok\n");
	return 0;
}
DRIVER
# Sanitizers when the runtime is actually there: several distros ship libasan
# in its own package, and linking -fsanitize= without it fails outright, which
# took the whole suite down with it. asan_usable() builds and RUNS a probe.
san=""
asan_usable && san="-fsanitize=address,undefined"
# shellcheck disable=SC2086
"$CC" -g -O1 $san -I"$work" -o "$work/driver" "$work/driver.c" \
	|| fail "driver failed to compile"
"$work/driver" || fail "comparator total-order checks failed (see output above)"

pass "rrd_name_compare is a total order (stable mixed sort, no phantom equality)"
