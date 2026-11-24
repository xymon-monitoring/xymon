#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/buildsystem/cppflags-honored.sh
#
# The build must honour CPPFLAGS from the environment, so a distribution's
# preprocessor flags survive -- notably -D_FORTIFY_SOURCE, which Debian's
# dpkg-buildflags delivers through CPPFLAGS (not CFLAGS). Before this, CPPFLAGS
# was named nowhere, so those flags were silently dropped and FORTIFY hardening
# was absent even when the packaging asked for it. This is the CPPFLAGS
# counterpart to the CFLAGS/LDFLAGS work in #163.
#
# Three things must hold, and each fails on a tree where CPPFLAGS is unused:
#
#  1. a compile rule passes $(CPPFLAGS) to the compiler -- checked by behaviour:
#     "make -n" of one object with CPPFLAGS set to a probe prints the probe on
#     the compile command;
#  2. build/Makefile.rules forwards CPPFLAGS to every sub-make it forwards
#     CFLAGS to, or the flag is dropped crossing into that directory;
#  3. every per-directory compile rule that names $(CFLAGS) also names
#     $(CPPFLAGS), so no compilation unit is left out.
#
# Checks 2 and 3 read the Makefiles: the build's own spelling is the surface
# here, as in the portability check's tests.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
# The tree is GNU make (lib/Makefile opens with an ifeq); on the BSDs
# /usr/bin/make is BSD make, which cannot read it. Use the GNU make the
# build uses.
require_gnu_make
probe="XYMON_CPPFLAGS_PROBE"

# 1. Behaviour: the probe reaches the compile command for a plain object.
out=$("$XYMON_MAKE" -Bn -C "$ROOT/lib" loadhosts.o CPPFLAGS="-D$probe" 2>/dev/null || true)
compile=$(printf '%s\n' "$out" | grep -E ' -c ' || true)
case $compile in
	*"-D$probe"*) : ;;
	*) fail "a CPPFLAGS define did not reach the compile command; make -n gave:
$compile" ;;
esac

# 2. build/Makefile.rules forwards CPPFLAGS wherever it forwards CFLAGS.
rules="$ROOT/build/Makefile.rules"
dropped=$(grep -F 'CFLAGS="$(CFLAGS)"' "$rules" | grep -vF 'CPPFLAGS="$(CPPFLAGS)"' || true)
[ -z "$dropped" ] || fail "a line forwards CFLAGS to a sub-make but not CPPFLAGS, so it is dropped there:
$dropped"

# 3. Every compile rule that names $(CFLAGS) also names $(CPPFLAGS).
for f in "$ROOT"/*/Makefile; do
	[ -f "$f" ] || continue
	mf=$(basename "$(dirname "$f")")
	# a compile line: invokes $(CC) and either compiles (-c) or names a source ($<)
	bad=$(grep -E '\$\(CC\)' "$f" \
		| grep -E -- '-c |\$<' \
		| grep -F '$(CFLAGS)' \
		| grep -vF '$(CPPFLAGS)' || true)
	[ -z "$bad" ] || fail "$mf/Makefile has a compile rule with \$(CFLAGS) but not \$(CPPFLAGS):
$bad"
done

pass "the build honours CPPFLAGS: a define reaches the compile command, build/Makefile.rules forwards it to every sub-make, and every compile rule names it"
