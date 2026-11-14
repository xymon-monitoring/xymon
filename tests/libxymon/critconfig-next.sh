#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/libxymon/critconfig-next.sh
#
# get_critconfig() steps on from a record a lookup found, in both variants of
# the tree (#802). The Critical Systems editor's Next button does exactly
# this, and the critical view's time filter tries a service's next time
# window the same way.
#
# In the tsearch variant xtreeNext() follows links only xtreeFirst()'s walk
# sets up, and the critical config was never walked after loading: Next from a
# looked-up record found nothing. In the array variant Next tested its position
# with "if (handle)", and the end of that tree is -1, so Next from the last
# record read the key of a record that is not there and crashed.
#
# Checked, in each variant:
#   - Next from alpha gives beta, and Next from h2|conn, the last record
#     before the clone h3=, which Next skips, nothing;
#   - the time filter on h1|conn, out of its time window, gives its second
#     window (h1|conn|1); on h1|http, out of its window, its second window
#     h1|http|1, past the h1|httpstatus that sorts between them, and never
#     h1|httpstatus itself; on h0|http, its second window written H0|HTTP;
#     on h2|conn, out of its window and followed in the
#     tree by the clone h3=, nothing, and no crash on the clone;
#   - after a clone is added, the record before it steps on to it; after it
#     is dropped, past it (no freed record); after gamma is deleted, Next
#     from beta steps past it.
#
# lib/loadcriticalconf.c and lib/tree.c are compiled once per variant, and
# lib/holidays.c too, the one other object they pull in that uses the tree (an
# archive member compiled for the other variant would pass positions of the
# wrong type); libxymoncomm.a gives the rest. Each variant gets a copy of
# include/ with config.h switched, and a lib link beside it, as libxymon.h
# reads "config.h" from its own directory and "../lib/" relative to it: every
# file then sees the one config.h. The array variant is always checked; the
# tsearch one where <search.h> has it, and is otherwise reported as not
# checked.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
require_c_buildenv "$ROOT"
[ -f "$ROOT/include/config.h" ] || skip "tree not configured (include/config.h absent)"
work=$(mktempdir)
build_xymon_libs "$ROOT" "$work/libbuild.log" libxymoncomm.a

# Far in the future: a time window that has not started
later=2000000000
{
	printf 'alpha.example.com|conn||||1|||\nbeta.example.com|conn||||1|||\ngamma.example.com|conn||||1|||\n'
	printf 'h1|conn|%s|||1|||\nh1|conn||||2|||\n' "$later"
	printf 'h1|http|%s|||1|||\nh1|http||||4|||\nh1|httpstatus||||3|||\n' "$later"
	printf 'h2|conn|%s|||1|||\nh3|=h1\n' "$later"
	# A second window in other case: the tree, ordered without case, keys it H0|HTTP|1
	printf 'h0|http|%s|||1|||\nH0|HTTP||||6|||\n' "$later"
} >"$work/critical.cfg"

cat >"$work/harness.c" <<'EOF'
#include <stdio.h>
#include <string.h>
#include "libxymon.h"

/* next KEY -- the key of the record NEXT gives after looking KEY up, or "-" */
static const char *next(char *key)
{
	char *rkey = NULL;

	if (!get_critconfig(key, CRITCONF_FIRSTMATCH, NULL)) return "lookup failed";
	if (!get_critconfig(NULL, CRITCONF_NEXT, &rkey) || !rkey) return "-";
	return rkey;
}

/* rawnext KEY -- the key of the record RAW_NEXT gives after looking KEY up, or "-" */
static const char *rawnext(char *key)
{
	char *rkey = NULL;

	if (!get_critconfig(key, CRITCONF_FIRSTMATCH, NULL)) return "lookup failed";
	if (!get_critconfig(NULL, CRITCONF_RAW_NEXT, &rkey) || !rkey) return "-";
	return rkey;
}

/* window KEY -- the priority of the record the time filter gives for KEY, or -1 */
static int window(char *key)
{
	critconf_t *rec = get_critconfig(key, CRITCONF_TIMEFILTER, NULL);

	return rec ? rec->priority : -1;
}

int main(int argc, char **argv)
{
	const char *got;
	int prio, fails = 0;

	if (load_critconfig(argv[1]) != 0) { printf("cannot load %s\n", argv[1]); return 2; }

	got = next("alpha.example.com|conn");
	if (strcmp(got, "beta.example.com|conn") != 0) { printf("Next from alpha gave %s, not beta\n", got); fails++; }
	got = next("h2|conn");
	if (strcmp(got, "-") != 0) { printf("Next from h2|conn, the last but a clone, gave %s, not nothing\n", got); fails++; }

	prio = window("h1|conn");
	if (prio != 2) { printf("the time filter on h1|conn gave priority %d, not its second window's 2\n", prio); fails++; }
	prio = window("h1|http");
	if (prio != 4) { printf("the time filter on h1|http gave priority %d, not its second window's 4 (h1|httpstatus's is 3)\n", prio); fails++; }
	prio = window("h0|http");
	if (prio != 6) { printf("the time filter on h0|http gave priority %d, not its second window's 6 (keyed H0|HTTP|1)\n", prio); fails++; }
	prio = window("h2|conn");
	if (prio != -1) { printf("the time filter on h2|conn gave priority %d, not nothing\n", prio); fails++; }

	/* A clone added between alpha and beta, then dropped */
	addclone_critconfig("alpha.example.com", "b");
	got = rawnext("alpha.example.com|conn");
	if (strcmp(got, "b=") != 0) { printf("after the clone b was added, the record after alpha was %s, not b=\n", got); fails++; }
	dropclone_critconfig("b");
	got = rawnext("alpha.example.com|conn");
	if (strcmp(got, "beta.example.com|conn") != 0) { printf("after the clone b was dropped, the record after alpha was %s, not beta\n", got); fails++; }

	delete_critconfig("gamma.example.com|conn", 1);
	got = next("beta.example.com|conn");
	if (strcmp(got, "h0|http") != 0) { printf("Next from beta, after gamma was deleted, gave %s, not h0|http\n", got); fails++; }
	return fails;
}
EOF

# tsearch where <search.h> has it, as xymon's configure decides
cat >"$work/probe.c" <<'EOF'
#include <search.h>
#include <stdlib.h>
static int c(const void*a,const void*b){(void)a;(void)b;return 0;}
int main(void){void*r=NULL;int x=1;(void)tsearch(&x,&r,c);return 0;}
EOF
variants=array
"$CC" -o "$work/probe" "$work/probe.c" 2>/dev/null && variants="array tsearch"

for variant in $variants; do
	inc=$work/$variant/include
	mkdir -p "$inc"
	cp "$ROOT"/include/*.h "$inc/"
	ln -s "$ROOT/lib" "$work/$variant/lib"
	if [ "$variant" = tsearch ]; then
		sed -e '/HAVE_BINARY_TREE/d' "$ROOT/include/config.h" >"$inc/config.h"
		echo '#define HAVE_BINARY_TREE 1' >>"$inc/config.h"
	else
		sed -e '/HAVE_BINARY_TREE/d' "$ROOT/include/config.h" >"$inc/config.h"
	fi
	# -iquote, and first: xymon_cflags names the build's include/ with -iquote
	# shellcheck disable=SC2046  # the flag lists are meant to split
	"$CC" -g -iquote "$inc" $(xymon_cflags "$ROOT") -o "$work/harness-$variant" "$work/harness.c" \
		"$ROOT/lib/loadcriticalconf.c" "$ROOT/lib/holidays.c" "$ROOT/lib/tree.c" "$(xymon_lib "$ROOT" libxymoncomm.a)" \
		$(xymon_ldflags "$ROOT") 2>"$work/cc-$variant.log" \
		|| fail "the $variant harness does not compile: $(cat "$work/cc-$variant.log")"
	rc=0
	out=$("$work/harness-$variant" "$work/critical.cfg" 2>&1) || rc=$?
	[ "$rc" -eq 0 ] || fail "the $variant tree (rc $rc): $out"
done

claim="get_critconfig() steps on from a looked-up record, stops after the last, keeps a time filter to its own service's windows, and steps on after a delete"
case $variants in
*tsearch*) pass "$claim, in both tree variants" ;;
*) pass_partial "$claim, in the array tree variant" "the tsearch variant: <search.h> has no tsearch() here" ;;
esac
