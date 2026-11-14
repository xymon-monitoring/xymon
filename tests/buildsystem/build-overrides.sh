#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/buildsystem/build-overrides.sh
#
# A build with no top-level Makefile -- CMake, an installed package -- runs
# the suite by naming its own library directory, compile flags and link
# flags. Checked on the helpers themselves: xymon_lib finds an archive in
# ROOT/lib or in XYMON_LIBDIR; xymon_cflags adds XYMON_CFLAGS to the tree's
# own include flags; xymon_ldflags returns XYMON_LDFLAGS when set;
# require_c_buildenv takes XYMON_CFLAGS for a configured tree; and
# build_xymon_libs does not rebuild what XYMON_LIBDIR holds, and skips when
# an archive is not there.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

# The helpers under test read these; the run's own must not answer for them.
unset XYMON_LIBDIR XYMON_CFLAGS XYMON_LDFLAGS

work=$(mktempdir)
mkdir -p "$work/tree/lib" "$work/tree/include" "$work/other"
: >"$work/other/libxymoncomm.a"

assert_equal "$work/tree/lib/libxymoncomm.a" "$(xymon_lib "$work/tree" libxymoncomm.a)" "xymon_lib, no override"
assert_equal "$work/other/libxymoncomm.a" "$(XYMON_LIBDIR=$work/other xymon_lib "$work/tree" libxymoncomm.a)" \
	"xymon_lib with XYMON_LIBDIR"

flags=$(XYMON_CFLAGS="-iquote $work/other" xymon_cflags "$work/tree")
assert_contains "-iquote $work/tree/include" "$flags" "xymon_cflags dropped the tree's own include path"
assert_contains "-iquote $work/other" "$flags" "xymon_cflags left out XYMON_CFLAGS"

assert_equal "-lpcre2-8 -lssl" "$(XYMON_LDFLAGS='-lpcre2-8 -lssl' xymon_ldflags "$work/tree")" "xymon_ldflags with XYMON_LDFLAGS"
assert_equal "" "$(xymon_ldflags "$work/tree")" "xymon_ldflags on a tree with no Makefile and no override"

# require_c_buildenv and build_xymon_libs skip by exiting 77, so each runs in
# a subshell whose exit status is the answer. require_c_buildenv is checked
# only where its own prerequisites hold -- a working CC and GNU make --
# since without them it skips before reaching the config.h check: a CC that
# is merely named, as autopkgtest names a nonexistent one, is not enough (#848).
if (require_cc; require_gnu_make) >/dev/null 2>&1; then
	rc=0; (require_c_buildenv "$work/tree") >/dev/null 2>&1 || rc=$?
	assert_equal 77 "$rc" "require_c_buildenv on a tree with no config.h and no XYMON_CFLAGS"
	rc=0; (XYMON_CFLAGS="-iquote $work/other"; require_c_buildenv "$work/tree") >/dev/null 2>&1 || rc=$?
	assert_equal 0 "$rc" "require_c_buildenv with XYMON_CFLAGS"
fi
rc=0; (XYMON_LIBDIR=$work/other; build_xymon_libs "$work/tree" "$work/log" libxymoncomm.a) >/dev/null 2>&1 || rc=$?
assert_equal 0 "$rc" "build_xymon_libs with the archive in XYMON_LIBDIR"
rc=0; (XYMON_LIBDIR=$work/other; build_xymon_libs "$work/tree" "$work/log" libxymontime.a) >/dev/null 2>&1 || rc=$?
assert_equal 77 "$rc" "build_xymon_libs with an archive missing from XYMON_LIBDIR"

pass "xymon_lib, xymon_cflags, xymon_ldflags, require_c_buildenv and build_xymon_libs take a build's own directory and flags"
