#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/buildsystem/install-server-dirs.sh
#
# The server's install-dirs creates every directory 755, and every parent
# mkdir -p makes for one, whatever the umask: a package built under umask 077
# would otherwise ship directories the xymon user cannot enter. Under PKGBUILD
# the chmod 755 after each mkdir is skipped and it never reached the parents,
# so only the umask the directories are created with decides their mode.
#
# "make install-dirs" alone, into a scratch INSTALLROOT, with PKGBUILD set so
# no chown needs root, and every server and client install path overridden
# into the stage so nothing outside it is touched. Each relocated directory
# is under a parent only it creates.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
[ -f "$ROOT/Makefile" ] || skip "the tree is not configured"
require_gnu_make

run_install() {  # run_install <stage> -- returns make's status
	local r=$1
	"$XYMON_MAKE" -C "$ROOT" install-dirs PKGBUILD=1 INSTALLROOT="$r" \
		XYMONUSER="$(id -un)" HTTPDGID="$(id -gn)" \
		XYMONTOPDIR=/xs XYMONHOME=/xs/server XYMONVAR=/xv/var \
		CGIDIR=/xs/cgi-bin SECURECGIDIR=/xs/cgi-secure \
		XYMONLOGDIR=/xl/log XYMONRUNDIR=/xr/run MANROOT=/xm/man \
		INSTALLBINDIR=/xb/bin INSTALLETCDIR=/xe/etc \
		INSTALLEXTDIR=/xx/ext INSTALLTMPDIR=/xt/tmp \
		INSTALLWEBDIR=/xw/web INSTALLWWWDIR=/xh/www INSTALLSTATICWWWDIR=/xg/static \
		INSTALLCLIENTETCDIR=/xc/etc INSTALLCLIENTBINDIR=/xc/bin \
		INSTALLCLIENTTMPDIR=/xc/tmp INSTALLCLIENTLOGDIR=/xc/logs \
		>"$r.log" 2>&1
}

work=$(mktempdir)

# ---- created under umask 077: every directory and parent is 755 -------------
stage=$work/stage
(umask 077; run_install "$stage") || fail "install-dirs failed: $(cat "$stage.log")"
for d in /xs/server /xs/server/download /xv/var/rrd /xb/bin /xe/etc /xh/www/rep /xg/static/help; do
	[ -d "$stage$d" ] || fail "install-dirs did not create $d"
done
bad=$(find "$stage" -type d ! -perm 755 -exec ls -ld {} \;)
[ -z "$bad" ] || fail "built under umask 077 with PKGBUILD, install-dirs left directories that are not drwxr-xr-x: $bad"

# ---- a directory that already exists keeps its mode --------------------------
mkdir -p "$work/existing/xv/var/rrd"
chmod 700 "$work/existing/xv/var/rrd"
run_install "$work/existing" || fail "install-dirs failed over an existing rrd directory: $(cat "$work/existing.log")"
mode=$(ls -ld "$work/existing/xv/var/rrd" | cut -c1-10)
[ "$mode" = drwx------ ] || fail "with PKGBUILD, install-dirs changed an existing directory's mode to $mode"

pass "install-dirs creates the server's directories and their parents 755 under umask 077 with PKGBUILD, and leaves an existing directory's mode alone"
