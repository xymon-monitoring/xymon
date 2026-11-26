#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/buildsystem/install-replaces-existing-names.sh
#
# The client install copies its programs into directories that already hold
# entries from an earlier install, including the relocation symlinks #411
# leaves at $XYMONCLIENTHOME/bin and /etc. When a name it installs is a
# symlink, the install must replace the name, not write through the link:
# otherwise cp -fp would write the program's content to the file the link
# points to. This installs with a symlink left at an installed name and checks
# the file it points to is untouched and the name is the real program.
#
# The copy runs for any user, so this needs no root. The ownership half (chown
# -h, not following such a link) is held by install-chown-nofollow.sh.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
[ -f "$ROOT/Makefile" ] || skip "the tree is not configured"
[ -f "$ROOT/client/xymonclient.cfg" ] || skip "the client is not built"
require_gnu_make

work=$(mktempdir)
home=/xc
stage=$work/stage

install_client() {
	"$XYMON_MAKE" -C "$ROOT" -o client install-client PKGBUILD=1 \
		INSTALLROOT="$stage" XYMONCLIENTHOME="$home" XYMONUSER="$(id -un)" \
		>"$work/install.log" 2>&1 || fail "install-client failed: $(cat "$work/install.log")"
}

install_client

# A file outside the install tree, with known content. A symlink left at an
# installed name must not reach it: the install replaces the name. runclient.sh
# is installed directly under XYMONHOME in every layout, so it is a stable name
# to plant at.
echo content-unchanged >"$work/pointed-at"
ln -sf "$work/pointed-at" "$stage$home/runclient.sh"

install_client

[ "$(cat "$work/pointed-at")" = content-unchanged ] \
	|| fail "install wrote through a symlink at runclient.sh to the file it points to"
[ ! -L "$stage$home/runclient.sh" ] \
	|| fail "install left runclient.sh a symlink instead of the script"
[ -s "$stage$home/runclient.sh" ] \
	|| fail "install did not place the real runclient.sh"

pass "the install replaces a symlink left at an installed name and does not write through it"
