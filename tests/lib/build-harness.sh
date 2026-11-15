# SPDX-License-Identifier: GPL-2.0-or-later
# shellcheck shell=bash
#
# tests/lib/build-harness.sh -- link a standalone test harness against the
# in-tree libraries. Source after assert.sh, and after the caller has built
# the archives it names (with build_xymon_libs), then e.g.:
#
#     build_xymon_libs "$ROOT" "$work/libbuild.log" libxymon.a
#     build_harness "$work" "$work/harness" "$here/foo-harness.c" libxymon.a
#
# After the log directory and the output binary come the inputs in link order:
# an argument ending in ".a" is an in-tree archive, resolved through xymon_lib;
# anything else is a source compiled in (the harness, and any production .c it
# drives directly). Sources are placed before the archives, and an archive may
# be named twice where a circular dependency needs it, as the plain
# "$CC ... a.c lib.a" line did. The build's own compile and link flags come
# from xymon_cflags/xymon_ldflags, and the PCRE2 libraries are resolved the way
# build_xymond_worker resolves them and appended -- an unused -lpcre2-8 on a
# harness that does not need it is harmless, and every harness that pulls in
# libxymon.h needs it.
#
# Extra compile flags -- an -iquote for a harness that includes a component's
# private header, or a -D it sets -- go in HARNESS_CFLAGS, which the caller
# exports for the one call.
#
# This collapses the repeated cflags/ldflags/pcre + "$CC" link + "does not
# compile" block; it does not build the archives or test a precondition, so a
# caller's build_xymon_libs call and any "tree not built" skip stay where they
# are and keep their meaning.

[ -n "${__XYMON_TESTS_BUILD_HARNESS_SOURCED:-}" ] && return 0
__XYMON_TESTS_BUILD_HARNESS_SOURCED=1

build_harness() {
	local logdir=$1 outbin=$2
	shift 2
	local root cc pcre_libs harness_cflags harness_ldflags arg sources archives

	root=$(find_root)
	cc=${CC:-cc}

	# PCRELIBS: explicit env override first, then the configured tree's own
	# setting (which carries any -L for a nonstandard install), then the bare
	# library as a last resort -- the same order as build_xymond_worker.
	pcre_libs=${PCRELIBS:-}
	[ -n "$pcre_libs" ] || pcre_libs=$(xymon_makevar "$root" PCRELIBS)
	[ -n "$pcre_libs" ] || pcre_libs="-lpcre2-8"

	harness_cflags=$(xymon_cflags "$root")
	harness_ldflags=$(xymon_ldflags "$root")

	sources=()
	archives=()
	for arg in "$@"; do
		case $arg in
			*.a) archives+=("$(xymon_lib "$root" "$arg")") ;;
			*)   sources+=("$arg") ;;
		esac
	done

	# shellcheck disable=SC2086 # the flag strings are intentionally word-split
	"$cc" $harness_cflags ${HARNESS_CFLAGS:-} -o "$outbin" \
		"${sources[@]}" "${archives[@]}" \
		$pcre_libs $harness_ldflags 2>"$logdir/cc.log" \
		|| { cat "$logdir/cc.log" >&2; fail "harness does not compile"; }
}
