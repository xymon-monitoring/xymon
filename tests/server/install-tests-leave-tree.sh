#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/install-tests-leave-tree.sh
#
# The tests that run "make install" with paths of their own leave the tree they
# run from as they found it. They used to run it in that tree: when the build
# id had changed since the build -- an uncommitted edit, a new commit --
# "make install" recompiled lib/environ.o there with the test's scratch paths,
# and a later test linking the library read them (tests/server/xymon-rundir.sh
# then expected the configured log directory and got a scratch one). CI never
# saw it: its build id does not move during a run.
#
# Checked on a copy of the tree, never on the tree itself: the copy's build id
# is changed, each install test is run from the copy, and the copy's environ.o
# must come out unchanged.
set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
require_gnu_make
if [ ! -f "$ROOT/Makefile" ] || [ ! -f "$ROOT/lib/environ.o" ]; then
	skip "tree not built (run make first; the post-build CI suite covers this)"
fi
require_bin XYMOND xymond/xymond

# The copy of the built tree and the install staging under it are large. Keep
# them on the same filesystem as the tree -- which is on disk, since the build
# wrote there -- rather than under $TMPDIR, which defaults to /tmp: NetBSD backs
# /tmp with a memory tmpfs too small to hold a built tree three times over (the
# tree, its copy, and the staged install). Fall back to $TMPDIR where no
# directory can be made beside the tree.
if disk_tmp=$(mktemp -d "$(dirname "$ROOT")/xymon-it.XXXXXX" 2>/dev/null); then
	register_cleanup "rm -rf -- $(printf '%q' "$disk_tmp")"
	export TMPDIR="$disk_tmp"
	work="$disk_tmp"
else
	work=$(mktempdir)
fi
copy=$(copy_source_tree "$ROOT" "$work/tree")
echo "a-different-build-id" > "$copy/build/build-id"
before=$(cksum < "$copy/lib/environ.o")

for t in install-empty-ext-dir install-static-wwwdir; do
	bash "$copy/tests/server/$t.sh" > "$work/$t.log" 2>&1 \
		|| fail "$t did not pass when run from a copy of the tree: $(cat "$work/$t.log")"
	[ "$(cksum < "$copy/lib/environ.o")" = "$before" ] \
		|| fail "after $t, lib/environ.o of the tree it ran from was rebuilt, with that test's paths"
done

pass "the install tests leave lib/environ.o of the tree they run from as it was, though its build id had changed"
