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

# Every makefile that installs: the server skeleton, the client, and each
# component that copies programs, extensions, manpages or config into a
# directory $(XYMONUSER) ends up owning.
MAKEFILES="build/Makefile.rules client/Makefile common/Makefile xymond/Makefile xymonnet/Makefile web/Makefile xymongen/Makefile xymonproxy/Makefile"

bad=""
rec=""
for f in $MAKEFILES; do
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
for f in $MAKEFILES; do
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

# chmod has no portable -h. A chmod on an install directory $(XYMONUSER) owns --
# a $(XYMONVAR) data subdir, one of the top install dirs (bin/etc/ext/tmp/web/
# www and $(XYMONHOME)/download), a log/run dir, or a drop-in .d dir -- must be
# guarded by "test -L ... ||" so it skips a symlink rather than following it to
# the target's mode. (A chmod on a file name under such a dir, e.g. .../foo.cfg,
# is covered instead by its cp/rm-f or test-L drop and is not matched here.)
cm=""
for f in $MAKEFILES; do
	[ -f "$ROOT/$f" ] || continue
	lines=$(sed "s/#.*//" "$ROOT/$f" | grep -nE "chmod .*(XYMONVAR\)/|INSTALL[A-Z]+DIR\)( |;|\$)|/tasks\.d|\\\$\\\$[a-z]|XYMONLOGDIR\)|XYMONRUNDIR\)|XYMONHOME\)/download|XYMONHOME\)( |;|$))" || true)
	[ -z "$lines" ] || while IFS= read -r l; do
		[ -n "$l" ] || continue
		case $l in *"test -L"*) ;; *) cm="$cm
$f: ${l#*:}" ;; esac
	done <<EOF
$lines
EOF
done
[ -z "$cm" ] || fail "a chmod on a \$(XYMONUSER)-owned install directory is not guarded by 'test -L ... ||', so a symlink there is followed to its target's mode:$cm"

# A setuid/setgid chmod (mode 2xxx/4xxx/6xxx) on an installed name is the worst
# to follow -- it would make a symlink's target setuid/setgid-root. It must be
# "test -L ... ||" guarded.
su=""
for f in $MAKEFILES; do
	[ -f "$ROOT/$f" ] || continue
	lines=$(sed "s/#.*//" "$ROOT/$f" | grep -nE "chmod [2467][0-7][0-7][0-7] .*INSTALLROOT" || true)
	[ -z "$lines" ] || while IFS= read -r l; do
		[ -n "$l" ] || continue
		case $l in *"test -L"*) ;; *) su="$su
$f: ${l#*:}" ;; esac
	done <<EOF
$lines
EOF
done
[ -z "$su" ] || fail "a setuid/setgid chmod on an installed name is not guarded by 'test -L ... ||', so a symlink there would have its target made setuid-root:$su"

# A "cp -fp ... $(INSTALLROOT)..." copies into a directory $(XYMONUSER) owns, so
# a symlink at the destination name would be written through. Each such line
# must drop the name first: a "rm -f" on the same line or the line before it
# (a per-name loop, or the "test -L ...; rm -f" of a preserved config).
co=""
for f in $MAKEFILES; do
	[ -f "$ROOT/$f" ] || continue
	prev=""
	while IFS= read -r line; do
		case $line in
		*"-fp "*INSTALLROOT*|*"ln -f "*INSTALLROOT*)
			case $prev$line in *"rm -f"*) ;; *) co="$co
$f: $line" ;; esac ;;
		esac
		prev=$line
	done <<EOF
$(sed "s/#.*//" "$ROOT/$f")
EOF
done
[ -z "$co" ] || fail "a copy into an install directory (\$(INSTALLROOT)...) is not preceded by a 'rm -f' of its destination, so a symlink at the name is written through:$co"

pass "no install makefile follows a symlink at an installed name: every chown/chgrp carries -h and is non-recursive, every cp drops the destination name first, and every chmod on a \$(XYMONVAR) subdirectory skips a symlink there"
