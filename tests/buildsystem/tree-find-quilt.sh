#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/buildsystem/tree-find-quilt.sh
#
# A Debian package build applies its patches with quilt, which keeps the
# original of every file a patch changes under .pc/<patch>/. A test walking
# the source tree read each patched file twice there, the second time as it
# was before the patch: strncat-bounds counted the backups as new files and
# failed with "the strncat baseline grew" (#977). tree_find, which every test
# walking the tree uses, leaves .pc and .git out.
#
# Checked twice: tree_find on a small tree with a .pc and a .git beside a
# source file, which must list the source file alone; and strncat-bounds run
# in an export of this tree -- no .git, as a release tarball unpacks, so it
# walks the tree rather than asking git -- with a quilt backup holding an
# unsafe strncat(), which it must not count.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
work=$(mktempdir)

# ---- tree_find itself -------------------------------------------------------
mkdir -p "$work/small/lib" "$work/small/.pc/fix.patch/lib" "$work/small/.git/objects"
: >"$work/small/lib/a.c"
: >"$work/small/.pc/fix.patch/lib/a.c"
: >"$work/small/.git/objects/b.c"
got=$(tree_find "$work/small" -type f -name '*.c')
assert_equal "lib/a.c" "$got" \
	"tree_find listed quilt's .pc or .git, not the source file alone"

# ---- strncat-bounds in an unpacked tree with a quilt backup ----------------
command -v git >/dev/null 2>&1 && git -C "$ROOT" rev-parse --verify -q HEAD >/dev/null \
	|| pass_partial "tree_find leaves quilt's .pc and .git out" \
		"strncat-bounds on an exported tree needs this tree's git to export it"
mkdir -p "$work/export"
git -C "$ROOT" archive HEAD | (cd "$work/export" && tar xf -) \
	|| fail "could not export the tree with git archive"
# This test's own helper, as it stands in the tree under test
cp "$ROOT/tests/lib/assert.sh" "$work/export/tests/lib/assert.sh"
cp "$ROOT/tests/buildsystem/strncat-bounds.sh" "$work/export/tests/buildsystem/strncat-bounds.sh"
mkdir -p "$work/export/.pc/fix.patch/lib"
printf 'void f(char *s) { char buf[8]; strncat(buf, s, sizeof(buf)); }\n' \
	>"$work/export/.pc/fix.patch/lib/quiltbackup.c"
out=$(bash "$work/export/tests/buildsystem/strncat-bounds.sh" 2>&1) \
	|| fail "strncat-bounds counted quilt's .pc backup as a source file: $out"
assert_contains "no strncat() bound lost its room" "$out" \
	"strncat-bounds did not run its check on the exported tree"

pass "tree_find leaves quilt's .pc and .git out, and strncat-bounds ignores a quilt backup in an unpacked tree"
