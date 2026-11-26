#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/buildsystem/install-replaces-existing-names.sh
#
# The client install writes into directories that already hold entries from an
# earlier install, including the relocation symlinks #411 leaves at
# $XYMONCLIENTHOME/bin and /etc. When a name it installs is a symlink, the
# install must replace the name, not act on what the link points to: cp -fp
# would write the file's content through the link, and chown without -h would
# change the linked file's owner. This installs with a symlink left at an
# installed name and checks the file the link points to is untouched.
#
# The ownership step runs only when install is not told PKGBUILD and can set a
# different owner, which needs root; skips otherwise. Run in a throwaway tree.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
[ -f "$ROOT/Makefile" ] || skip "the tree is not configured"
[ -f "$ROOT/client/xymonclient.cfg" ] || skip "the client is not built"
require_gnu_make
[ "$(id -u)" = 0 ] || skip "needs root (the ownership step sets a different owner)"
id nobody >/dev/null 2>&1 || skip "needs a 'nobody' user for the ownership step"

work=$(mktempdir)
home=/xc
stage=$work/stage

install_client() {
	"$XYMON_MAKE" -C "$ROOT" -o client install-client \
		INSTALLROOT="$stage" XYMONCLIENTHOME="$home" XYMONUSER=nobody \
		>"$work/install.log" 2>&1 || fail "install-client failed: $(cat "$work/install.log")"
}

install_client

# Two files outside the install tree. A symlink left at an installed name must
# not reach them: the install replaces the name instead of following the link.
echo content-unchanged >"$work/content-file"
echo owner-unchanged >"$work/owner-file"
chown root:root "$work/content-file" "$work/owner-file"

ln -sf "$work/content-file" "$stage$home/bin/xymonlaunch"
ln -sf "$work/owner-file"   "$stage$home/etc/localclient.cfg"

install_client

# The copy replaced the name; it did not write through the link.
[ "$(cat "$work/content-file")" = content-unchanged ] \
	|| fail "install wrote through a symlink at bin/xymonlaunch to a file outside the tree"
[ ! -L "$stage$home/bin/xymonlaunch" ] \
	|| fail "install left bin/xymonlaunch a symlink instead of the program"

# The ownership step acted on the link, not on the file it points to.
owner=$(ls -ld "$work/owner-file" | awk '{print $3}')
[ "$owner" = root ] \
	|| fail "install set the owner of a file outside the tree, through a symlink at etc/localclient.cfg, to $owner"

pass "the install replaces a symlink left at an installed name and does not act on the file it points to"
