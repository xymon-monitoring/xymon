#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/buildsystem/configure-exec-bits.sh
#
# Every shell script a Makefile runs from build/ gets its execute bit back
# from configure, for a tree that lost it on the way -- an export, an
# archive, a copy. configure restores build/*.sh only, so a script without
# the suffix stays non-executable and the build stops on it: make install
# failed with "../build/convert-bbservices: Permission denied" until the
# script was renamed convert-bbservices.sh (#626).
#
# Checked on a copy of configure, of those scripts and of client/*.sh, every
# execute bit removed: after `configure --help`, each script the Makefiles
# name is executable again. The scripts are found by reading the Makefiles,
# so a new one without the suffix fails here too. Compiled helpers that live
# in build/ (merge-sects, ...) are built, not shipped, and are left out.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)

work=$(mktempdir)
t="$work/tree"
mkdir -p "$t/build" "$t/client"

# The build/ files the Makefiles run, as shipped scripts: a #! first line.
scripts=""
for name in $(find "$ROOT" -name 'Makefile*' -type f ! -path "$ROOT/tests/*" -exec cat {} + \
		| grep -oE '(^|[^[:alnum:]_.-])build/[[:alnum:]_.-]+' \
		| sed 's|.*build/||' | sort -u); do
	[ -f "$ROOT/build/$name" ] || continue
	[ "$(head -c 2 "$ROOT/build/$name")" = '#!' ] || continue
	scripts="$scripts $name"
	cp "$ROOT/build/$name" "$t/build/"
done
[ -n "$scripts" ] || fail "found no script in build/ that a Makefile runs: the search is broken"

cp "$ROOT"/configure* "$t/"
cp "$ROOT"/client/*.sh "$t/client/"
# Read and write kept for the owner: only the execute bit goes, as when a
# tree is exported without it.
chmod 644 "$t"/configure* "$t"/build/* "$t"/client/*

sh "$t/configure" --help >"$work/configure.out" 2>&1 \
	|| fail "configure --help failed: $(cat "$work/configure.out")"

for name in $scripts; do
	[ -x "$t/build/$name" ] \
		|| fail "a Makefile runs build/$name, and configure does not make it executable again: in a tree that lost the bit, the build stops there. Name it *.sh"
done

pass "configure restores the execute bit of every script the Makefiles run from build/ (${scripts# })"
