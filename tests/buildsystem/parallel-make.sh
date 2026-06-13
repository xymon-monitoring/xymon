#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/buildsystem/parallel-make.sh
#
# Regression guard for the legacy-Makefile parallel-build race fixed
# by xymon-monitoring/xymon#76 (refs #3) and #92 (refs #91). Both PRs
# added missing prerequisites in build/Makefile.rules and lib/Makefile
# so that 'make -jN' does not race when fetching headers from lib/ or
# when rebuilding the build/ helpers.
#
# This is a *static* check by design: actually exercising 'make -jN'
# is already done by .github/workflows/build.yml (which uses -jN per
# #76), and a separate behavioural run would cost minutes per CI cycle
# while telling us nothing the build workflow doesn't already cover.
# What we check here is that the dependency *declarations* survive
# future Makefile edits; without them, parallel builds regress
# silently and intermittently.
#
# When the in-progress CMake migration replaces these Makefiles, this
# test skips cleanly via the [-f $RULES] / [-f $LIB] guards below and
# can be retired alongside the legacy build system.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
RULES="$ROOT/build/Makefile.rules"
LIB="$ROOT/lib/Makefile"

[ -f "$RULES" ] || skip "build/Makefile.rules absent (CMake-only tree?)"
[ -f "$LIB" ]   || skip "lib/Makefile absent"

rules=$(cat "$RULES")
lib=$(cat "$LIB")

# --- #76 (refs #3) -----------------------------------------------------------

# build-build must depend on either lib-build or lib-client, not stand
# alone. Without this, the build/ helpers start compiling before lib/
# has produced the headers they include.
assert_match 'build-build:[[:space:]]+include/config\.h[[:space:]]+lib-(build|client)' \
             "$rules" \
             "build-build must depend on lib-build or lib-client (#3/#76)"

# lib-client (the non-CLIENTONLY arm) must depend on lib-build so the
# client variant doesn't compile against half-built shared headers.
assert_contains "lib-client: include/config.h lib-build" \
                "$rules" \
                "lib-client must depend on lib-build in the non-CLIENTONLY arm (#3/#76)"

# sha1.o, rmd160c.o and the standalone md5/sha1/rmd160 binaries each
# shell out to ./test-endianness during their build line. They need
# test-endianness as an explicit prereq so -jN can't try to compile
# them before it exists. Match by exact target line so we don't confuse
# the object-file rule (sha1.o:) with the standalone binary rule (sha1:).
for line in \
	"sha1.o: sha1.c test-endianness" \
	"rmd160c.o: rmd160c.c test-endianness" \
	"md5: md5.c test-endianness" \
	"sha1: sha1.c test-endianness" \
	"rmd160: rmd160c.c test-endianness"; do
	assert_contains "$line" "$lib" \
	                "lib/Makefile must contain '$line' (#3/#76)"
done

# --- #92 (refs #91) ----------------------------------------------------------

# common-client (the non-CLIENTONLY arm) must depend on common-build
# so the client variant doesn't race with the server variant on the
# shared common/ output dir.
assert_contains "common-client: lib-client common-build" \
                "$rules" \
                "common-client must depend on common-build in the non-CLIENTONLY arm (#91/#92)"
