#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/install-tools.sh
#
# "make install-tools" installs the diagnostic programs a server build
# compiles and nothing else installs, under the names the build gives
# them: lib/'s xymonloadhosts, xymonstackio, xymonavailability and
# xymonlocator, and xymonnet's xymonnetprobe. Not lib/'s tree, which tests
# the xtree code and shows nothing about an installation.
# Their manual page, xymontools.1, is installed with them, and not by a
# plain install-man.
#
# A client-only build compiles none of them. Run there, the target used to
# build lib/'s tools on demand, install them, and then fail linking
# xymonnetprobe without c-ares -- an install that stops half done. It now
# refuses before installing anything.
#
# Needs a configured server tree with the tools built, so it runs after the
# build in CI's server leg and skips elsewhere.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
[ -f "$ROOT/Makefile" ] || skip "the tree is not configured"
require_gnu_make
# A server build, which builds the tools. Not the tools themselves: built
# under another name, they would turn this test into a skip.
require_bin XYMOND xymond/xymond

tools="xymonloadhosts xymonstackio xymonavailability xymonlocator xymonnetprobe"

run_install_tools() {  # run_install_tools <root> [VAR=value...] -- returns make's status
	local r=$1; shift
	"$XYMON_MAKE" -C "$ROOT" install-tools PKGBUILD=1 \
		INSTALLROOT="$r" INSTALLBINDIR=/bin MANROOT=/man "$@" >"$r.log" 2>&1
}

# ---- a server build installs exactly the five ------------------------------
work=$(mktempdir)
mkdir -p "$work/server"
run_install_tools "$work/server" \
	|| fail "install-tools failed in a server build: $(cat "$work/server.log")"

for t in $tools; do
	[ -f "$work/server/bin/$t" ] || fail "install-tools did not install $t"
	[ -x "$work/server/bin/$t" ] || fail "install-tools installed $t without execute permission"
	usage=$("$work/server/bin/$t" --help 2>/dev/null) || :
	grep -q "^Usage: .*/$t " <<<"$usage" \
		|| fail "the installed $t does not run, or its --help does not name it as installed"
done
got=$(ls "$work/server/bin" | sort | tr '\n' ' ')
want=$(printf '%s\n' $tools | sort | tr '\n' ' ')
[ "$got" = "$want" ] \
	|| fail "install-tools installed [$got], expected exactly [$want]"

# The build tree carries the same names, so a copy from it needs no renaming.
for t in $tools; do
	[ -x "$ROOT/lib/$t" ] || [ -x "$ROOT/xymonnet/$t" ] \
		|| fail "the build did not produce $t under the name it is installed by"
done

# ---- the tools' manual page comes with them, and only with them -----------
[ -f "$work/server/man/man1/xymontools.1" ] \
	|| fail "install-tools did not install the tools' manual page man1/xymontools.1"
mkdir -p "$work/plain"
"$XYMON_MAKE" -C "$ROOT" install-man PKGBUILD=1 INSTALLROOT="$work/plain" MANROOT=/man \
	>"$work/plain.log" 2>&1 || fail "install-man failed: $(cat "$work/plain.log")"
[ -f "$work/plain/man/man1/xymoncfg.1" ] \
	|| fail "install-man installed no man1 pages at all, so the next check proves nothing"
[ ! -e "$work/plain/man/man1/xymontools.1" ] \
	|| fail "install-man installed xymontools.1, a page for programs only install-tools installs"

# ---- a client-only build refuses, and installs nothing ---------------------
mkdir -p "$work/client"
rc=0; run_install_tools "$work/client" CLIENTONLY=yes || rc=$?
[ "$rc" -ne 0 ] \
	|| fail "install-tools reported success in a client-only build, which builds none of the tools"
left=$(find "$work/client" -type f | sed "s#^$work/client##" | tr '\n' ' ')
[ -z "$left" ] \
	|| fail "install-tools in a client-only build installed [$left] before failing -- half an install"
grep -q 'client-only build does not build these diagnostics' "$work/client.log" \
	|| fail "install-tools in a client-only build failed without saying why: $(cat "$work/client.log")"

pass "install-tools installs the five diagnostics in a server build, and refuses a client-only build without installing anything"
