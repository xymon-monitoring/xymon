#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/rrd/rrddef-empty-list.sh
#
# Both RRD-definition tables count entries with a do/while that dereferences
# before it tests, so an empty list -- a 1-byte allocation holding only the NUL
# -- makes the first strchr(lenv+1, ',') read past it.
#
# Two checks: the guarded loop is in the source for both tables, and the real
# lib/xymonrrd.c runs under ASan through find_xymon_rrd()/find_xymon_graph()
# with every list empty. Production code, not a copy: a copy cannot stop the
# original drifting away from it.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
SRC="$ROOT/lib/xymonrrd.c"
CC="${CC:-cc}"

[ -f "$SRC" ] || skip "lib/xymonrrd.c absent"
src=$(cat "$SRC")

# (1) the guarded loop is in the source, for both tables.
guarded="count = 0; if (*lenv) { p = lenv; do { count++; p = strchr(p+1, ','); } while (p); }"
guards=$(grep -cF "$guarded" "$SRC" || true)
assert_equal "2" "$guards" \
	"xymonrrd.c must guard the comma-count loop of BOTH tables (xymonrrds and xymongraphs) against an empty list"
assert_not_contains "count = 0; p = lenv; do" "$src" \
	"xymonrrd.c regressed to the unguarded comma-count loop: on an empty list it reads one byte past the allocation"

# (2) production xymonrrd.c under ASan, every list empty.
require_cc
WORK=$(mktempdir)

# Probed on its own: folding it into the harness build reported any compile
# error at all as "no ASan" and passed the test.
asan_usable || pass "xymonrrd.c empty-list count loops are guarded (static check; $CC cannot build and run ASan binaries)"

# The second half needs a built tree. Without one the first has still run, so
# that is what gets reported rather than a failure over a missing precondition.
[ -f "$ROOT/include/config.h" ] && [ -f "$ROOT/lib/libxymoncomm.a" ] \
	|| pass "xymonrrd.c empty-list count loops are guarded (static check; tree not configured or not built)"

build_xymon_libs "$ROOT" "$WORK/libbuild.log" libxymoncomm.a
# Configured flags, not a hand-picked SSLLIBS: without the rpath the harness
# links on NetBSD and then cannot find libpcre2-8 at run time.
harness_cflags=$(xymon_cflags "$ROOT")
harness_ldflags=$(xymon_ldflags "$ROOT")
pcre_libs=$(sed -n 's/^PCRELIBS *= *//p' "$ROOT/Makefile")
[ -n "$pcre_libs" ] || pcre_libs="-lpcre2-8"

# Compiled in rather than taken from the archive: the loop has to be
# instrumented, and the mutant below has to be swappable.
build_case() {  # build_case <output> <xymonrrd.c to use>
	# shellcheck disable=SC2086
	"$CC" $harness_cflags -fsanitize=address -o "$1" \
		"$(dirname "$0")/rrddef-empty-list-harness.c" "$2" \
		"$ROOT/lib/libxymoncomm.a" $pcre_libs $harness_ldflags 2>"$WORK/cc.log"
}

# An empty protocols.cfg is what empties the TCP service list; with no file at
# all init_tcp_services() falls back to its defaults and the case is missed.
mkdir -p "$WORK/etc"
: >"$WORK/etc/protocols.cfg"

run_case() {  # run_case <binary>
	env XYMONHOME="$WORK" TEST2RRD="" GRAPHS="" \
		ASAN_OPTIONS=detect_leaks=0 "$1" 2>&1
}

build_case "$WORK/fixed" "$SRC" \
	|| { cat "$WORK/cc.log" >&2; fail "the harness does not compile against production xymonrrd.c"; }
out=$(run_case "$WORK/fixed") \
	|| fail "building the RRD-definition tables from empty lists faulted under ASan: $out"
assert_not_contains "AddressSanitizer" "$out" \
	"AddressSanitizer flagged the table setup on empty lists"

# The contrast: same source, guard removed, must be flagged -- a harness that
# can no longer see the fault looks exactly like a passing one.
unguarded="count = 0; p = lenv; do { count++; p = strchr(p+1, ','); } while (p);"
awk -v g="$guarded" -v u="$unguarded" '
	{
		while ((i = index($0, g)) > 0) {
			$0 = substr($0, 1, i-1) u substr($0, i + length(g))
			n++
		}
		print
	}
	END { if (n != 2) exit 1 }
' "$SRC" > "$WORK/buggy.c" \
	|| fail "expected the guarded loop twice in xymonrrd.c; the mutant was not built"
build_case "$WORK/buggy" "$WORK/buggy.c" \
	|| { cat "$WORK/cc.log" >&2; fail "the unguarded variant does not compile"; }
out=$(run_case "$WORK/buggy" || true)
assert_contains "AddressSanitizer" "$out" \
	"ASan did not flag the unguarded loop -- the harness cannot see the regression it guards"

pass "empty RRD-definition lists: production table setup clean under ASan, unguarded loop flagged"
