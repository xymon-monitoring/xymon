#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/buildsystem/probe-generic-fallback.sh
#
# configure's library probes read Makefile.generic on a platform that has no
# Makefile.<OS> of its own, as configure itself does for the build.
#
# Each build/Makefile.test-* included Makefile.$(OS) outright. On a platform
# without one -- any the tree has no makefile for, and since #111 the ones it
# dropped -- every probe died on the missing file, and configure reported
# PCRE, SSL, RRD and the rest as absent while the build went on against
# Makefile.generic.
#
# Each probe is run as configure runs it, from build/ with OS set and one of
# its own targets named -- the default target would be the first rule of the
# included makefile -- but under "make -n": make reads its includes before
# anything else, so a missing Makefile.<OS> still stops it, while nothing is
# compiled, and the result does not depend on which libraries this system has.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
require_gnu_make

os=XymonNoSuchOS
[ ! -e "$ROOT/build/Makefile.$os" ] || fail "build/Makefile.$os exists; the test needs an OS name with no makefile"
[ -f "$ROOT/build/Makefile.generic" ] || fail "build/Makefile.generic is missing"

n=0
for f in "$ROOT"/build/Makefile.test-*; do
	probe=${f##*/}
	target=$(awk -F: '/^[a-z][a-z0-9-]*:/ { print $1; exit }' "$f")
	[ -n "$target" ] || fail "$probe has no target of its own to run"
	out=$(cd "$ROOT/build" && OS=$os "$XYMON_MAKE" -n -f "$probe" "$target" 2>&1) \
		|| fail "$probe stops on a platform with no Makefile.<OS> instead of reading Makefile.generic: $out"
	n=$((n + 1))
done
[ "$n" -gt 0 ] || fail "no build/Makefile.test-* probe found"

pass "the $n configure probes read Makefile.generic on a platform with no Makefile.<OS>"
