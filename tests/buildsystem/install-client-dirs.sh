#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/buildsystem/install-client-dirs.sh
#
# The client's etc, bin, tmp and logs can be installed elsewhere, as the
# server's can: INSTALLCLIENT{ETC,BIN,TMP,LOG}DIR, which a client-only build
# also fills from the INSTALLETCDIR/INSTALLBINDIR/INSTALLTMPDIR that
# configure.client collects. Each relocated directory is linked back from its
# old place under XYMONCLIENTHOME, so a path written anywhere upstream keeps
# resolving. Unset, nothing moves and nothing is linked.
#
# A real "make install-client" into a scratch INSTALLROOT, with the build step
# declared done (-o client) so it installs this tree's built files, PKGBUILD
# set so no chown needs root, and every client directory overridden.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
[ -f "$ROOT/Makefile" ] || skip "the tree is not configured"
[ -f "$ROOT/client/xymonclient.cfg" ] || skip "the client is not built"
require_gnu_make

home=/xc

run_install() {  # run_install <root> [VAR=value...] -- returns make's status
	local r=$1; shift
	"$XYMON_MAKE" -C "$ROOT" -o client install-client PKGBUILD=1 \
		INSTALLROOT="$r" XYMONCLIENTHOME="$home" XYMONUSER="$(id -un)" "$@" \
		>"$r.log" 2>&1
}

work=$(mktempdir)

# ---- unset: the old layout, nothing linked ----------------------------------
# A client-only tree's Makefile already carries the INSTALLETCDIR, BINDIR
# and TMPDIR its configure took, which this change honours; empty them for
# the layout nothing asked to move.
unset_dirs="INSTALLETCDIR= INSTALLBINDIR= INSTALLTMPDIR="
mkdir -p "$work/plain"
# shellcheck disable=SC2086  # separate assignments by design
run_install "$work/plain" $unset_dirs || fail "install-client failed in the default layout: $(cat "$work/plain.log")"
for d in etc bin tmp logs; do
	[ -d "$work/plain$home/$d" ] && [ ! -L "$work/plain$home/$d" ] \
		|| fail "in the default layout, $home/$d is not a real directory"
done
[ -f "$work/plain$home/etc/xymonclient.cfg" ] || fail "the default layout put no xymonclient.cfg in $home/etc"
[ -f "$work/plain$home/bin/xymonclient.sh" ] || fail "the default layout put no xymonclient.sh in $home/bin"

# ---- set: files where asked, the old paths linked to them -------------------
reloc="INSTALLCLIENTETCDIR=/etc/xymon-client INSTALLCLIENTBINDIR=/usr/libexec/xymon-client
INSTALLCLIENTTMPDIR=/var/tmp/xymon-client INSTALLCLIENTLOGDIR=/var/log/xymon-client"
mkdir -p "$work/moved"
# shellcheck disable=SC2086  # the four assignments are separate words by design
run_install "$work/moved" $reloc || fail "install-client failed with the directories relocated: $(cat "$work/moved.log")"
[ -f "$work/moved/etc/xymon-client/xymonclient.cfg" ] \
	|| fail "INSTALLCLIENTETCDIR was not used: no xymonclient.cfg in /etc/xymon-client"
[ -f "$work/moved/usr/libexec/xymon-client/xymonclient.sh" ] \
	|| fail "INSTALLCLIENTBINDIR was not used: no xymonclient.sh in /usr/libexec/xymon-client"
for pair in etc:/etc/xymon-client bin:/usr/libexec/xymon-client tmp:/var/tmp/xymon-client logs:/var/log/xymon-client; do
	d=${pair%%:*} to=${pair#*:}
	[ -d "$work/moved$to" ] || fail "the relocated $d directory $to was not created"
	[ -L "$work/moved$home/$d" ] || fail "$home/$d was not linked to its new place $to"
	got=$(readlink "$work/moved$home/$d")
	[ "$got" = "$to" ] || fail "$home/$d links to '$got', expected '$to'"
done

# ---- a client-only build: the values configure.client collects --------------
# Here the INSTALLCLIENT*DIR are worked out inside the Makefile, from
# INSTALLETCDIR, INSTALLBINDIR and INSTALLTMPDIR, so the client's own Makefile
# sees them only if install-client passes them down. Set on the command line,
# make would hand them to it by itself and this would prove nothing.
mkdir -p "$work/clientonly"
run_install "$work/clientonly" CLIENTONLY=yes INSTALLETCDIR=/etc/xymon-c \
	INSTALLBINDIR=/usr/lib/xymon-c/bin INSTALLTMPDIR=/var/tmp/xymon-c \
	|| fail "install-client failed for a client-only build with its directories set: $(cat "$work/clientonly.log")"
[ -f "$work/clientonly/etc/xymon-c/xymonclient.cfg" ] \
	|| fail "a client-only build ignored INSTALLETCDIR: no xymonclient.cfg in /etc/xymon-c"
[ -f "$work/clientonly/usr/lib/xymon-c/bin/xymonclient.sh" ] \
	|| fail "a client-only build ignored INSTALLBINDIR: no xymonclient.sh in /usr/lib/xymon-c/bin"
[ -d "$work/clientonly/var/tmp/xymon-c" ] && [ -L "$work/clientonly$home/tmp" ] \
	|| fail "a client-only build ignored INSTALLTMPDIR"

# ---- and again: the same result, no error -----------------------------------
# shellcheck disable=SC2086
run_install "$work/moved" $reloc || fail "a second relocated install failed: $(cat "$work/moved.log")"
[ "$(readlink "$work/moved$home/etc")" = /etc/xymon-client ] || fail "a second install changed the $home/etc link"

pass "install-client puts the client's etc, bin, tmp and logs where INSTALLCLIENT*DIR say, or a client-only build's INSTALLETCDIR/BINDIR/TMPDIR, links the old paths to them, and changes nothing when they are unset"
