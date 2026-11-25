#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/buildsystem/configure-make.sh
#
# configure_make (tests/lib/assert.sh) is what a test calls before running
# configure: on a host whose make(1) is not GNU make and that has no gmake,
# it skips, where the test used to run configure into its GNU make check and
# fail for a reason it is not about (#1043); where GNU make is gmake, as on
# the BSDs, it exports MAKE=gmake, which configure takes.
#
# Each case runs it in a subshell whose PATH holds only stubs: a make that
# answers as BSD make does, and in the second case a gmake that answers as
# GNU make. configure_make and skip use nothing else outside the shell.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

work=$(mktempdir)
mkdir -p "$work/bsd" "$work/gnu"
printf '#!/bin/sh\necho "make: unknown option -- -" >&2\nexit 2\n' >"$work/bsd/make"
printf '#!/bin/sh\necho "GNU Make 4.4.1"\n' >"$work/gnu/gmake"
cp "$work/bsd/make" "$work/gnu/make"
chmod +x "$work/bsd/make" "$work/gnu/make" "$work/gnu/gmake"

rc=0
(unset XYMON_MAKE MAKE; PATH=$work/bsd; configure_make) 2>/dev/null || rc=$?
assert_equal 77 "$rc" "configure_make with no GNU make on PATH should skip"

rc=0
got=$(unset XYMON_MAKE MAKE; PATH=$work/gnu; configure_make; printf '%s' "$MAKE") || rc=$?
assert_equal 0 "$rc" "configure_make with GNU make as gmake should not skip"
assert_equal gmake "$got" "configure_make with GNU make as gmake should export MAKE=gmake"

pass "configure_make skips without GNU make, and hands configure gmake where make(1) is not GNU make"
