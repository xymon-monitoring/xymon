#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/buildsystem/install-tools.sh
#
# "make install-tools" installs the diagnostic programs a server build
# compiles and nothing else installs: lib/'s loadhosts, stackio,
# availability, locator and tree, and xymonnet's contest.
#
# A client-only build compiles none of them. Run there, the target used to
# build lib/'s five on demand, install them, and then fail linking contest
# without c-ares -- an install that stops half done. It now refuses before
# installing anything.
#
# Needs a configured server tree with the tools built, so it runs after the
# build in CI's server leg and skips elsewhere.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
[ -f "$ROOT/Makefile" ] || skip "the tree is not configured"
require_gnu_make
require_bin LOADHOSTS lib/loadhosts
require_bin CONTEST xymonnet/contest

tools="loadhosts stackio availability locator tree contest"

run_install_tools() {  # run_install_tools <root> [VAR=value...] -- returns make's status
	local r=$1; shift
	"$XYMON_MAKE" -C "$ROOT" install-tools PKGBUILD=1 \
		INSTALLROOT="$r" INSTALLBINDIR=/bin "$@" >"$r.log" 2>&1
}

# ---- a server build installs exactly the six -------------------------------
work=$(mktempdir)
mkdir -p "$work/server"
run_install_tools "$work/server" \
	|| fail "install-tools failed in a server build: $(cat "$work/server.log")"

for t in $tools; do
	[ -f "$work/server/bin/$t" ] || fail "install-tools did not install $t"
	[ -x "$work/server/bin/$t" ] || fail "install-tools installed $t without execute permission"
done
got=$(ls "$work/server/bin" | sort | tr '\n' ' ')
want=$(printf '%s\n' $tools | sort | tr '\n' ' ')
[ "$got" = "$want" ] \
	|| fail "install-tools installed [$got], expected exactly [$want]"

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

pass "install-tools installs the six diagnostics in a server build, and refuses a client-only build without installing anything"
