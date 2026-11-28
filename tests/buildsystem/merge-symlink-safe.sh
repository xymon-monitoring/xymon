#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/buildsystem/merge-symlink-safe.sh
#
# merge-sects and merge-lines merge a shipped configuration template into the
# administrator's installed file. They run as root over a file in a directory
# the unprivileged Xymon user owns, so they must not follow a symlink planted at
# the destination name -- neither reading it (which would copy the link's target
# into the .bak) nor writing through it. This compiles both and asserts it.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
work=$(mktempdir)
cd "$work"

for tool in merge-sects merge-lines; do
	[ -f "$ROOT/build/$tool.c" ] || { skip "build/$tool.c not present"; continue; }
	${CC:-cc} ${CFLAGS:-} -o "$tool" "$ROOT/build/$tool.c" || fail "$tool.c did not compile"

	rm -rf d && mkdir d && cd d
	printf 'a=1\nb=2\n' > template
	printf 'SECRET\n' > secret
	ln -sf "$work/d/secret" dest

	# The tool takes TEMPLATE then the destination.
	"../$tool" template dest >/dev/null 2>&1 || true

	[ "$(cat secret)" = SECRET ] || fail "$tool followed a symlink at the destination and touched its target"
	[ -f dest ] && [ ! -L dest ] || fail "$tool left the destination a symlink instead of a real file"
	if [ -f dest.bak ] && grep -q SECRET dest.bak; then fail "$tool copied the symlink target into the .bak"; fi
	cd ..
done

pass "merge-sects and merge-lines do not follow a symlink at the destination name"
