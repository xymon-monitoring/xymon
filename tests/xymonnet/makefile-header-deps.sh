#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/xymonnet/makefile-header-deps.sh
#
# The make build rebuilds an xymonnet object when a header in xymonnet/ that
# its source includes changes. The Makefile listed none of them but
# udpquery.h, so changing
# xymonnet.h -- the layout of testitem_t -- left the other objects built
# against the old layout, and an incremental build crashed in
# send_http_results().
#
# For each local header, GNU make's -n -W (as if the file had changed, nothing
# touched) must list exactly the objects whose source includes it: none
# missing, which is the stale build, and none extra. The expected set comes
# from the #include lines of each source, and of a source it includes in turn
# (xymonnet.c includes ntpprobe.c), so a header added later and included from a
# source is covered without editing this test; one included only from another
# header is not followed. xymonnetprobe, which compiles contest.c itself, must be
# rebuilt as well. Headers outside xymonnet/ (include/, lib/) are not covered: no
# Makefile in the tree lists them.

set -euo pipefail
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
require_gnu_make
[ -f "$ROOT/xymonnet/xymonnet.o" ] || skip "no make-built xymonnet objects (a CMake build tracks headers itself)"

cd "$ROOT/xymonnet"
objs=$(sed -n 's/^NETTESTOBJS = //p' Makefile)
[ -n "$objs" ] || fail "NETTESTOBJS not found in xymonnet/Makefile"

checked=0
for h in *.h; do
	want=""
	for o in $objs; do
		src=${o%.o}.c
		inc=$(sed -n 's/^#include "\([a-z0-9_-]*\.c\)".*/\1/p' "$src")
		# shellcheck disable=SC2086 # the included sources are meant to split
		grep -q "^#include \"$h\"" "$src" $inc && want="$want ${o}"
	done
	[ -n "$want" ] || continue
	got=$("$XYMON_MAKE" -n -W "$h" $objs 2>/dev/null | sed -n 's/.* -o \([a-z0-9]*\.o\) .*/\1/p' | sort -u | tr '\n' ' ')
	want=$(printf '%s\n' $want | sort -u | tr '\n' ' ')
	assert_equal "$want" "$got" "objects make rebuilds when $h changes"
	checked=$((checked + 1))
done
# xymonnetprobe compiles contest.c in its own recipe: it is rebuilt when a
# header contest.c includes changes -- through its own prerequisites, or
# through httptest.o and dns.o, which it links and which depend on the same.
for h in *.h; do
	grep -q "^#include \"$h\"" contest.c || continue
	out=$("$XYMON_MAKE" -n -W "$h" xymonnetprobe 2>/dev/null || true)
	grep -q -- '-o xymonnetprobe' <<<"$out" \
		|| fail "make does not rebuild xymonnetprobe when $h, which contest.c includes, changes"
done

[ "$checked" -gt 0 ] || fail "no xymonnet object includes a local header: the check found nothing to check"

pass "make rebuilds each xymonnet object when a local header it includes changes ($checked headers)"
