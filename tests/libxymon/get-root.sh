#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/libxymon/get-root.sh
#
# Pins that get_root() is not fatal: run by a user who cannot become root,
# drop_root() then get_root() returns, the effective uid unchanged, where it
# used to abort with a core dump. Run as root, as the CI lanes are, the
# harness checks that root is given back, and a child that gives root up
# for good checks the user case too: that case fails without the fix either
# way.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
here=$(cd "$(dirname "$0")" && pwd)

require_c_buildenv "$ROOT"
[ -f "$(xymon_lib "$ROOT" libxymonclient.a)" ] \
	|| skip "tree not built (run make first; the post-build CI suite covers this)"

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

build_xymon_libs "$ROOT" "$work/libbuild.log" libxymonclient.a

harness_cflags=$(xymon_cflags "$ROOT")
harness_ldflags=$(xymon_ldflags "$ROOT")
# shellcheck disable=SC2086  # the flag lists are word lists
"$CC" $harness_cflags -o "$work/harness" "$here/get-root-harness.c" \
	"$(xymon_lib "$ROOT" libxymonclient.a)" $harness_ldflags 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; fail "harness does not compile"; }

rc=0
"$work/harness" >"$work/out" 2>&1 || rc=$?
[ "$rc" = 0 ] || { cat "$work/out" >&2; fail "drop_root() then get_root() as uid $(id -u) exited $rc"; }

if [ "$(id -u)" = 0 ]; then
	pass "get_root() gives root back to root, and returns, unprivileged, for uid 65534"
else
	pass "get_root() returns, unprivileged, for a user who cannot become root"
fi
