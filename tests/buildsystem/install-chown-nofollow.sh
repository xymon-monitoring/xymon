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
# recurse with find ... -exec chown -h instead. A "test ! -f DEST; then cp"
# (install a default only when absent) likewise follows a symlink left at DEST,
# so each must drop one at the name first. This reads the makefiles, so it runs
# on any platform and needs no build.

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

# "install a default only when absent" (test ! -f DEST; then cp ... DEST) would
# follow a symlink left at DEST -- test -f follows it, and cp writes through it.
# Each such line must first drop a symlink at the name (test -L DEST; rm -f).
tf=""
for f in client/Makefile common/Makefile xymond/Makefile build/Makefile.rules; do
	[ -f "$ROOT/$f" ] || continue
	lines=$(sed "s/#.*//" "$ROOT/$f" | grep -nE "test ! -f.*cp " || true)
	[ -z "$lines" ] || while IFS= read -r l; do
		[ -n "$l" ] || continue
		case $l in *"test -L"*) ;; *) tf="$tf
$f: ${l#*:}" ;; esac
	done <<EOF
$lines
EOF
done
[ -z "$tf" ] || fail "a 'test ! -f ... cp' line in an install makefile does not drop a symlink at the name first (test -L ...; rm -f), so a symlink there is followed:$tf"

# chmod has no portable -h. A chmod on a $(XYMONVAR) subdirectory -- which
# $(XYMONUSER) owns and may relocate with a symlink -- must be guarded by
# "test -L ... ||" so it skips a symlink rather than following it to the
# target's mode.
cm=""
for f in build/Makefile.rules client/Makefile common/Makefile xymond/Makefile; do
	[ -f "$ROOT/$f" ] || continue
	lines=$(sed "s/#.*//" "$ROOT/$f" | grep -nE "chmod .*XYMONVAR\)/" || true)
	[ -z "$lines" ] || while IFS= read -r l; do
		[ -n "$l" ] || continue
		case $l in *"test -L"*) ;; *) cm="$cm
$f: ${l#*:}" ;; esac
	done <<EOF
$lines
EOF
done
[ -z "$cm" ] || fail "a chmod on a \$(XYMONVAR) subdirectory is not guarded by 'test -L ... ||', so a relocation symlink there is followed to its target's mode:$cm"

pass "no install makefile follows a symlink at an installed name: every chown/chgrp carries -h and is non-recursive, every conditional cp drops a symlink at the name first, and every chmod on a \$(XYMONVAR) subdirectory skips a symlink there"
