#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/buildsystem/configure-rrd-probe-compiler.sh
#
# build/rrd.sh tells RRDtool's two argv APIs apart by compiling rrd.h against
# both prototypes of rrd_update(). Every other configure check, and the build,
# compiles with the compiler build/Makefile.$(OS) chooses; these two probes
# called ${CC:-cc} themselves. Where that choice is gcc and there is no cc --
# Solaris 11.4 -- both probes failed, and configure stopped with "Unable to
# determine the RRDtool argv API from the installed headers". They now run
# through build/Makefile.test-rrd like the other checks.
#
# Here build/rrd.sh runs on a copy, against a stub rrd.h, with CC unset, the
# compiler given to make as xymon-cc (MAKEFLAGS, so a command-line setting,
# whatever build/Makefile.$(OS) would choose), and a cc on PATH: both are
# wrappers that log their name and run the real compiler. The probes must
# reach an answer and use the compiler rrd.sh's own compile check uses,
# xymon-cc. On main they called cc. Which answer they reach depends on the
# host: where the build's flags carry -I/usr/local/include or the like, the
# host's own rrd.h comes before the stub, as for the compile check and the
# build.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
[ -f "$ROOT/build/rrd.sh" ] || skip "build/rrd.sh missing"

real=""
for c in gcc cc clang; do
	real=$(command -v "$c" 2>/dev/null || true)
	[ -n "$real" ] && break
done
[ -n "$real" ] || skip "no C compiler"

TMP=$(mktempdir)
mkdir -p "$TMP/src" "$TMP/inc" "$TMP/bin"
cp -R "$ROOT/build" "$TMP/src/build"
printf 'int rrd_update(int, char **);\n' >"$TMP/inc/rrd.h"
for name in cc xymon-cc; do
	printf '#!/bin/sh\necho "%s $*" >>"%s"\nexec "%s" "$@"\n' "$name" "$TMP/cc.log" "$real" >"$TMP/bin/$name"
	chmod +x "$TMP/bin/$name"
done
: >"$TMP/cc.log"

make=make
version=$(make --version 2>/dev/null || true)
grep -q 'GNU Make' <<<"$version" || make=gmake

rc=0
(cd "$TMP/src" && unset CC && PATH="$TMP/bin:$PATH" USERRRDINC="$TMP/inc" MAKE="$make" \
	MAKEFLAGS="CC=$TMP/bin/xymon-cc" \
	sh -c '. build/rrd.sh') >"$TMP/rrd.log" 2>&1 || rc=$?

log=$(cat "$TMP/rrd.log")
assert_contains "Detected RRDtool argv API: " "$log" \
	"the RRDtool probes reached no answer (rc=$rc)"

# Which compiler compiled what: rrd.sh's compile check builds test-rrd.c; every
# other compile (-c) is one of the two probes.
calls=$(cat "$TMP/cc.log")
check_cc=$(grep -e 'test-rrd\.c' <<<"$calls" | awk '{print $1}' | sort -u || true)
probe_cc=$(grep -v -e 'test-rrd\.c' -e '-dumpversion' <<<"$calls" | grep -e ' -c ' | awk '{print $1}' | sort -u || true)
[ -n "$check_cc" ] || fail "rrd.sh's compile check did not run a compiler: $calls"
[ -n "$probe_cc" ] || fail "the RRDtool probes did not run a compiler: $calls"
assert_equal "$check_cc" "$probe_cc" \
	"the RRDtool probes compile with a different compiler than rrd.sh's compile check"

pass "configure's RRDtool probes compile with make's compiler, as its other checks do"
