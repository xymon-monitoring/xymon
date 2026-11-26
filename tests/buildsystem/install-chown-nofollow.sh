#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/buildsystem/install-chown-nofollow.sh
#
# The install sets ownership in directories that hold the relocation symlinks
# #411 leaves (at $XYMONCLIENTHOME/bin, /etc and so on). A chown or chgrp
# without -h follows such a link and changes the file it points to, not the
# link; and a recursive "chown -R" is worse -- it is rejected on the BSDs with
# -h, and dereferences on Solaris, AIX and HP-UX without it. So every
# chown/chgrp in the install makefiles carries -h and none is recursive;
# recurse with find ... -exec chown -h instead. This reads the makefiles, so
# it runs on any platform and needs no build.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)

bad=""
rec=""
for f in build/Makefile.rules client/Makefile common/Makefile xymond/Makefile; do
	[ -f "$ROOT/$f" ] || continue
	src=$(sed "s/#.*//" "$ROOT/$f")
	calls=$(printf '%s\n' "$src" | grep -nE "(^|[[:space:]&|;])(chown|chgrp)[[:space:]]" || true)
	miss=$(printf '%s\n' "$calls" | grep -vE "(chown|chgrp)[[:space:]]+-[A-Za-z]*h" | grep . || true)
	[ -z "$miss" ] || bad="$bad
$f:
$miss"
	recf=$(printf '%s\n' "$calls" | grep -E "(chown|chgrp)[[:space:]]+-[A-Za-z]*R" | grep . || true)
	[ -z "$recf" ] || rec="$rec
$f:
$recf"
done

[ -z "$bad" ] || fail "a chown/chgrp in an install makefile lacks -h, so it follows a relocation symlink to the file it points to:$bad"
[ -z "$rec" ] || fail "a recursive chown/chgrp in an install makefile: -hR is rejected on the BSDs and -R dereferences on Solaris/AIX/HP-UX; recurse with find ... -exec chown -h:$rec"

pass "every chown/chgrp in the install makefiles carries -h and none is recursive, so none follows a relocation symlink"
