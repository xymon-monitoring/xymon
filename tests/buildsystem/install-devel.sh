#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/buildsystem/install-devel.sh
#
# "make install-devel" installs the development kit -- the static
# libraries, the headers and xymon.pc -- and the kit has one job: a program
# built with nothing but "pkg-config --cflags --libs --static xymon" must
# compile, link and run. That is checked by building one against an
# installed kit.
#
# Around it, what the target must and must not leave behind:
#
#   - a client-only build makes neither libxymon nor libxymoncomm; there
#     the target used to install three client archives under a xymon.pc
#     naming libraries that were not there. It now refuses, installing
#     nothing;
#   - the $XYMONHOME/sdk link is for a kit relocated as a tree
#     (INSTALLSDKDIR). Split across FHS trees there is no one directory to
#     point at, and the link used to land on the headers alone;
#   - under PKGBUILD nothing is created beneath $XYMONHOME: a -devel
#     package's staging root holds the kit and nothing else.
#
# Needs a configured server tree with the libraries built, so it runs after
# the build in CI's server leg and skips elsewhere.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
[ -f "$ROOT/Makefile" ] || skip "the tree is not configured"
# A built server, so lib/ holds libxymon and libxymoncomm. The tree's
# archives are only installed here, never linked: the consumer below links
# the installed kit, through xymon.pc alone.
require_bin XYMOND xymond/xymond
require_gnu_make
require_cc
command -v pkg-config >/dev/null 2>&1 || skip "pkg-config is needed to read xymon.pc"

home=/xh

run_install_devel() {  # run_install_devel <root> [VAR=value...] -- returns make's status
	local r=$1; shift
	"$XYMON_MAKE" -C "$ROOT" install-devel XYMONHOME="$home" \
		INSTALLROOT="$r" "$@" >"$r.log" 2>&1
}

work=$(mktempdir)

# ---- the default kit, and a program built from it ---------------------------
# Installed at a real path -- INSTALLROOT empty, XYMONHOME under $work -- so
# xymon.pc names where the kit actually is. A staged kit read through
# PKG_CONFIG_SYSROOT_DIR does not work: pkg-config prefixes every path with
# the sysroot, the system ones too, and the BSDs' -I/usr/local/include then
# points into the staging root, where pcre2.h is not.
kit=$work/kit
mkdir -p "$kit"
"$XYMON_MAKE" -C "$ROOT" install-devel PKGBUILD=1 INSTALLROOT= \
	XYMONHOME="$kit" >"$work/kit.log" 2>&1 \
	|| fail "install-devel failed in a server build: $(cat "$work/kit.log")"

for f in lib/libxymon.a lib/libxymoncomm.a lib/libxymontime.a include/libxymon.h lib/pkgconfig/xymon.pc; do
	[ -f "$kit/sdk/$f" ] || fail "install-devel did not install sdk/$f"
done

cat >"$work/consumer.c" <<'C'
#include <stdio.h>
#include <time.h>
#include "libxymon.h"

int main(void)
{
	strbuffer_t *b = newstrbuffer(0);
	sendresult_t (*send)(char *, char *, int, sendreturn_t *) = sendmessage;

	addtobuffer(b, "kit");
	printf("%s %d %d\n", STRBUF(b), getcurrenttime(NULL) > 0, send != NULL);
	return 0;
}
C

pcflags=$(PKG_CONFIG_PATH="$kit/sdk/lib/pkgconfig" \
	pkg-config --cflags --libs --static xymon) \
	|| fail "pkg-config could not read the installed xymon.pc"
# shellcheck disable=SC2086  # pkg-config output is a word list by design
"$CC" -o "$work/consumer" "$work/consumer.c" $pcflags 2>"$work/cc.log" \
	|| fail "a program built with only 'pkg-config --cflags --libs --static xymon' did not compile and link against the kit:
$(cat "$work/cc.log")"
out=$("$work/consumer") || fail "the program built against the kit did not run"
[ "$out" = "kit 1 1" ] || fail "the program built against the kit printed '$out', expected 'kit 1 1'"

# ---- PKGBUILD split: nothing at all beneath $XYMONHOME -----------------------
# The packager's -devel layout: headers and archives in FHS trees, no link.
# $XYMONHOME belongs to the server package; the kit must not create it.
mkdir -p "$work/pkg"
run_install_devel "$work/pkg" PKGBUILD=1 \
	INSTALLINCDIR=/usr/include/xymon INSTALLLIBDIR=/usr/lib64/xymon \
	|| fail "install-devel failed for a PKGBUILD split install: $(cat "$work/pkg.log")"
[ -f "$work/pkg/usr/lib64/xymon/libxymon.a" ] || fail "a PKGBUILD split install put no libxymon.a in INSTALLLIBDIR"
[ ! -e "$work/pkg$home" ] \
	|| fail "install-devel under PKGBUILD created \$XYMONHOME ($home) in the staging root, which holds only the kit"

# ---- a client-only build refuses, and installs nothing -----------------------
mkdir -p "$work/client"
rc=0; run_install_devel "$work/client" PKGBUILD=1 CLIENTONLY=yes || rc=$?
[ "$rc" -ne 0 ] \
	|| fail "install-devel reported success in a client-only build, which builds neither libxymon nor libxymoncomm"
left=$(find "$work/client" ! -type d | sed "s#^$work/client##" | tr '\n' ' ')
[ -z "$left" ] \
	|| fail "install-devel in a client-only build installed [$left] before failing"
grep -q 'client-only build does not build libxymon' "$work/client.log" \
	|| fail "install-devel in a client-only build failed without saying why: $(cat "$work/client.log")"

# ---- the $XYMONHOME/sdk link: a tree relocation gets one, a split does not ----
me=$(id -un)
mkdir -p "$work/split"
run_install_devel "$work/split" XYMONUSER="$me" \
	INSTALLINCDIR=/usr/include/xymon INSTALLLIBDIR=/usr/lib/xymon \
	|| fail "install-devel failed for a split install: $(cat "$work/split.log")"
[ -f "$work/split/usr/lib/xymon/libxymon.a" ] || fail "a split install put no libxymon.a in INSTALLLIBDIR"
[ -f "$work/split/usr/include/xymon/include/libxymon.h" ] || fail "a split install put no libxymon.h in INSTALLINCDIR"
if [ -e "$work/split$home/sdk" ] || [ -L "$work/split$home/sdk" ]; then
	fail "a split install left \$XYMONHOME/sdk pointing at $(readlink "$work/split$home/sdk" 2>/dev/null || echo '?'), which holds only part of the kit"
fi

mkdir -p "$work/tree"
run_install_devel "$work/tree" XYMONUSER="$me" INSTALLSDKDIR=/opt/xymon-sdk \
	|| fail "install-devel failed for a relocated tree: $(cat "$work/tree.log")"
[ -f "$work/tree/opt/xymon-sdk/lib/libxymon.a" ] || fail "a relocated tree put no libxymon.a under INSTALLSDKDIR"
[ -L "$work/tree$home/sdk" ] || fail "a kit relocated as a tree left no \$XYMONHOME/sdk link"
target=$(readlink "$work/tree$home/sdk")
[ "$target" = /opt/xymon-sdk ] || fail "\$XYMONHOME/sdk points at '$target', expected /opt/xymon-sdk"

pass "install-devel installs a kit a program builds against with pkg-config alone, refuses a client-only build, links \$XYMONHOME/sdk only for a tree relocation, and creates no \$XYMONHOME under PKGBUILD"
