#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/install-relink-flags.sh
#
# An install that finds a program older than what it links relinks it with
# the build's flags. The install-bin targets of common, xymongen and xymonnet
# depend on their programs, so a stale one is rebuilt inside the install
# sub-make; the install calls passed it install paths only, so it was linked
# without SSLLIBS and the rest, and the install failed with undefined
# references to OpenSSL (#865). Programs older than their libraries are what a
# changed build id leaves behind (#867).
#
# Run on a copy of the paths these installs reach, so the tree under test is
# left as it was. One program of each of common, xymongen and xymonnet is made
# older than everything it links, by dating it in 2000 rather than by waiting
# for the clock, and install-xymongen and install-xymonnet must then relink
# all three and install a xymongen and a xymonnet that run.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
require_gnu_make
[ -f "$ROOT/Makefile" ] || skip "requires a configured source tree with a top-level Makefile"
require_bin XYMONGEN xymongen/xymongen
require_bin XYMONNET xymonnet/xymonnet
require_bin XYMONCMD common/xymon

work=$(mktempdir)
tree=$work/tree
mkdir "$tree"
for p in Makefile build include lib common xymongen xymonnet; do
	cp -R "$ROOT/$p" "$tree/"
done

aged="common/xymon xymongen/xymongen xymonnet/xymonnet"
# Built with SNMP, xymon-snmpcollect links SNMPLIBS, which only the prefix gives
# the install sub-make.
[ -x "$tree/xymonnet/xymon-snmpcollect" ] && aged="$aged xymonnet/xymon-snmpcollect"
for p in $aged; do touch -t 200001010000 "$tree/$p"; done
touch -t 200101010000 "$work/aged"

dest=$work/inst
for target in install-xymongen install-xymonnet; do
	"$XYMON_MAKE" -C "$tree" "$target" INSTALLROOT= PKGBUILD=1 \
		XYMONUSER="$(id -un)" HTTPDGID="$(id -gn)" \
		XYMONTOPDIR="$dest/top" XYMONHOME="$dest/top/server" XYMONVAR="$dest/var" \
		CGIDIR="$dest/top/cgi-bin" SECURECGIDIR="$dest/top/cgi-secure" \
		XYMONLOGDIR="$dest/log" XYMONRUNDIR="$dest/run" MANROOT="$dest/man" \
		INSTALLBINDIR="$dest/top/server/bin" INSTALLETCDIR="$dest/etc" \
		INSTALLEXTDIR="$dest/top/server/ext" INSTALLTMPDIR="$dest/var/tmp" \
		INSTALLWEBDIR="$dest/etc/web" INSTALLWWWDIR="$dest/var/www" \
		INSTALLSTATICWWWDIR="$dest/var/www" >"$work/$target.log" 2>&1 \
		|| fail "$target failed on programs older than what they link: $(tail -n 5 "$work/$target.log")"
done

for p in $aged; do
	[ "$tree/$p" -nt "$work/aged" ] \
		|| fail "the install did not relink $p, so this run checked nothing"
done
for p in xymongen xymonnet; do
	"$dest/top/server/bin/$p" --version >"$work/$p.version" 2>&1 \
		|| fail "the installed $p does not run: $(cat "$work/$p.version")"
done

pass "an install that finds common, xymongen and xymonnet programs older than what they link relinks them with the build's flags"
