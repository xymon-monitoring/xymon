# SPDX-License-Identifier: GPL-2.0-or-later
# shellcheck shell=bash
#
# tests/lib/build-worker.sh -- compile a xymond worker from the configured
# tree for a test. Source after assert.sh, then e.g.:
#
#     build_xymond_worker "$outdir" xymond_hostdata \
#         xymond/xymond_hostdata.c xymond/xymond_worker.c
#
# Source paths are relative to the repo root; the binary lands in
# <outdir>/<name>.
#
# Skips (not fails) when the toolchain or a configured tree is absent -
# those are missing preconditions of the environment. Once both are
# present, a library or worker that fails to build is a product
# regression and FAILS the test, exactly as the pre-refactor tests did:
# demoting it to skip would let a build-breaking change turn every
# worker test green-by-absence.

[ -n "${__XYMON_TESTS_BUILD_WORKER_SOURCED:-}" ] && return 0
__XYMON_TESTS_BUILD_WORKER_SOURCED=1

build_xymond_worker() {
	local outdir=$1 prog=$2 root cc pcre_libs harness_ldflags harness_cflags src srcs
	shift 2
	root=$(find_root)
	cc=${CC:-cc}

	command -v "$cc" >/dev/null 2>&1 || skip "no C compiler available (CC=$cc)"
	require_gnu_make
	[ -f "$root/include/config.h" ] || skip "tree not configured (no include/config.h)"
	[ -f "$root/Makefile" ] || skip "tree not configured (no Makefile)"

	# PCRELIBS: explicit env override first, then the configured tree's own
	# setting (which carries any -L for a nonstandard install), then
	# pkg-config, then the bare library as a last resort.
	pcre_libs=${PCRELIBS:-}
	[ -n "$pcre_libs" ] || pcre_libs=$(sed -n 's/^PCRELIBS *= *//p' "$root/Makefile")
	if [ -z "$pcre_libs" ] && command -v pkg-config >/dev/null 2>&1; then
		pcre_libs=$(pkg-config --libs libpcre2-8 2>/dev/null || true)
	fi
	[ -n "$pcre_libs" ] || pcre_libs="-lpcre2-8"
	harness_cflags=$(xymon_cflags "$root")
	harness_ldflags=$(xymon_ldflags "$root")

	"$XYMON_MAKE" -C "$root/lib" libxymon.a libxymoncomm.a libxymontime.a >"$outdir/libbuild.log" 2>&1 \
		|| { cat "$outdir/libbuild.log" >&2; fail "the xymon libraries do not build in this configured tree"; }

	srcs=()
	for src in "$@"; do srcs+=("$root/$src"); done

	# Archives listed twice rather than --start-group, which is GNU ld only.
	"$cc" $harness_cflags -iquote "$root/xymond" -o "$outdir/$prog" \
		"${srcs[@]}" \
		"$root/lib/libxymon.a" "$root/lib/libxymoncomm.a" "$root/lib/libxymontime.a" \
		"$root/lib/libxymon.a" \
		$pcre_libs $harness_ldflags 2>"$outdir/cc.log" \
		|| { cat "$outdir/cc.log" >&2; fail "$prog does not build -- cannot run this test"; }
}
