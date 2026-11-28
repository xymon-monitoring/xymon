#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/buildsystem/setup-newfiles-symlink-safe.sh
#
# setup-newfiles installs a shipped default config or www file where none is
# present. It runs as root into a directory the unprivileged Xymon user owns, so
# a symlink planted at a destination name must not be followed: the shipped file
# must land at the name itself and the link's target must be left untouched.
# setup-newfiles delegates the placement to install-safe, which replaces the
# symlink rather than writing through it, so both tools must be built; the test
# skips where the build is absent.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin SETUP_NEWFILES build/setup-newfiles
require_bin INSTALLSAFE build/install-safe

work=$(mktempdir)
mkdir "$work/src" "$work/dest"
echo "DEFAULT CONTENT" > "$work/src/foo.cfg"

# A dangling symlink at the destination: stat() returns ENOENT, so setup-newfiles
# takes its "install new file" path. Following the link would create the victim;
# install-safe instead replaces the link at the name.
ln -s "$work/victim" "$work/dest/foo.cfg"

( cd "$work/src"; echo "foo.cfg" | "$SETUP_NEWFILES" "$work/dest/" ) >"$work/out" 2>&1 \
	|| fail "setup-newfiles failed on a symlinked destination: $(cat "$work/out")"

[ ! -e "$work/victim" ] || fail "setup-newfiles followed the symlink and created its target"
[ -f "$work/dest/foo.cfg" ] && [ ! -L "$work/dest/foo.cfg" ] \
	|| fail "setup-newfiles left the destination a symlink instead of a real file"
[ "$(cat "$work/dest/foo.cfg")" = "DEFAULT CONTENT" ] \
	|| fail "setup-newfiles did not install the shipped content at the name"

pass "setup-newfiles places the shipped file at the name and leaves a planted symlink's target untouched"
