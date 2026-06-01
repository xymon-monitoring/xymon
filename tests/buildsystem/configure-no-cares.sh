#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/buildsystem/configure-no-cares.sh
#
# A server build needs c-ares: xymonnet's DNS code calls ares_* throughout.
# When the c-ares probe fails, './configure --server' must stop, and say what
# to install. It used to fall back to a copy of c-ares 1.15.0 shipped in the
# tree, from 2018 and carrying the CVEs fixed since, which is gone.
#
# Approach, as in configure-no-rrd.sh: copy what configure --server touches
# into a scratch directory, stub the probes that run before c-ares so they
# succeed, and make c-ares fail to compile by replacing build/Makefile.test-cares
# -- the real build/c-ares.sh still decides what to do about it.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
[ -f "$ROOT/configure.server" ] || skip "configure.server missing"
[ -f "$ROOT/build/c-ares.sh" ] || skip "build/c-ares.sh missing"

TMP=$(mktempdir)
SRC="$TMP/src"
mkdir -p "$SRC"
cp -p "$ROOT/configure" "$ROOT/configure.server" "$SRC/"
cp -rp "$ROOT/build" "$SRC/build"
[ -f "$ROOT/configure.client" ] && cp -p "$ROOT/configure.client" "$SRC/"

# The probes before c-ares, made to succeed.
printf 'FPING="/bin/true"\n' >"$SRC/build/fping.sh"
printf 'PCREOK="YES"; PCREINCDIR=""; PCRELIBS="-lpcre2-8"\n' >"$SRC/build/pcre.sh"

# c-ares headers that cannot be compiled against, as on a host without them.
cat >"$SRC/build/Makefile.test-cares" <<'EOF'
test-compile:
	@echo "test-cares.c: fatal error: ares.h: No such file or directory (mocked)" >&2; exit 1

test-link:
	@exit 1

ares-clean:
	@true
EOF

cd "$SRC"
make_is_gnu() {
	local version
	version=$(first_line "$("$1" -version 2>&1 || true)")
	[ "$(awk '{print $1 " " $2}' <<<"$version")" = "GNU Make" ]
}
if [ -z "${MAKE:-}" ] && ! make_is_gnu make; then
	if command -v gmake >/dev/null 2>&1 && make_is_gnu gmake; then
		export MAKE=gmake
	fi
fi

LOG="$TMP/configure.log"
rc=0
USEXYMONPING=y ./configure --server </dev/null >"$LOG" 2>&1 || rc=$?

[ "$rc" -ne 0 ] \
	|| fail "configure --server went on without a usable c-ares, instead of stopping: $(sed -n '1,30p' "$LOG")"
grep -q "It is REQUIRED to build xymonnet" "$LOG" \
	|| fail "configure --server stopped without saying c-ares is required: $(sed -n '1,30p' "$LOG")"
[ ! -f "$SRC/Makefile" ] || fail "configure --server wrote a Makefile although c-ares is missing"

pass "configure --server stops when c-ares cannot be used, and says what to install"
