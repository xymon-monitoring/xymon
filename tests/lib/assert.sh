# SPDX-License-Identifier: GPL-2.0-or-later
# shellcheck shell=bash
#
# tests/lib/assert.sh -- minimal helpers for tests/ scenarios.
#
# Source from a test:
#     . "$(dirname "$0")/../lib/assert.sh"
#
# Contract documented in tests/README.md. The intent here is to give
# tests just enough vocabulary to read clearly, without growing into a
# framework. Resist adding features until a concrete test needs one.

# Guard against double-sourcing.
[ -n "${__XYMON_TESTS_ASSERT_SOURCED:-}" ] && return 0
__XYMON_TESTS_ASSERT_SOURCED=1

# ---- result reporting --------------------------------------------------------

# fail MSG -- print on stderr and exit non-zero (CI treats as failure).
fail() {
	printf 'FAIL: %s\n' "$*" >&2
	exit 1
}

# skip REASON -- print on stderr and exit 77 (CI treats as skipped, not failed;
# matches the autotools / autopkgtest convention). Use when a precondition for
# the test is genuinely absent, not to paper over a real failure.
skip() {
	printf 'SKIP: %s\n' "$*" >&2
	exit 77
}

# pass [MSG] -- cosmetic; tests that reach the end without failing already
# pass. Useful only when a test wants to emit a one-line success summary.
pass() {
	printf 'PASS: %s\n' "${*:-ok}"
	exit 0
}

# ---- assertions --------------------------------------------------------------

# assert_equal WANT GOT [MSG]
assert_equal() {
	local want=$1 got=$2 msg=${3:-}
	if [ "$want" != "$got" ]; then
		fail "${msg:+$msg: }expected '$want', got '$got'"
	fi
}

# assert_contains NEEDLE HAYSTACK [MSG]
assert_contains() {
	local needle=$1 haystack=$2 msg=${3:-}
	case "$haystack" in
		*"$needle"*) ;;
		*) fail "${msg:+$msg: }'$haystack' does not contain '$needle'" ;;
	esac
}

# assert_not_contains NEEDLE HAYSTACK [MSG]
assert_not_contains() {
	local needle=$1 haystack=$2 msg=${3:-}
	case "$haystack" in
		*"$needle"*) fail "${msg:+$msg: }'$haystack' unexpectedly contains '$needle'" ;;
	esac
}

# assert_match REGEX STRING [MSG] -- bash =~ regex
assert_match() {
	local re=$1 s=$2 msg=${3:-}
	if ! [[ $s =~ $re ]]; then
		fail "${msg:+$msg: }'$s' does not match /$re/"
	fi
}

# assert_file_exists PATH [MSG]
assert_file_exists() {
	local p=$1 msg=${2:-}
	[ -e "$p" ] || fail "${msg:+$msg: }file does not exist: $p"
}

# ---- fixtures / cleanup ------------------------------------------------------

# register_cleanup CMD... -- append a cleanup command that runs on EXIT.
# Stackable: each call adds to the chain. Use for temp dirs, killed
# processes, restored files. Tests that need cleanup should call this
# rather than installing their own trap, so multiple registrations
# compose.
#
# CONTRACT: the arguments are joined and stored as a single string that is
# later eval'd (see __xymon_tests_run_cleanups). This is deliberate -- it
# lets a registered command defer glob/expansion to teardown time (the
# mktempdir cleanup below relies on it). The flip side is that any dynamic
# path passed in must be shell-escaped by the caller; a bare
#     register_cleanup rm -rf "$dir"
# word-splits at eval if $dir holds whitespace or metacharacters. Escape it:
#     register_cleanup "rm -rf -- $(printf '%q' "$dir")"
__XYMON_TESTS_CLEANUP_CMDS=()
__xymon_tests_run_cleanups() {
	local rc=$? i
	# Run in reverse registration order so teardown mirrors setup.
	for (( i=${#__XYMON_TESTS_CLEANUP_CMDS[@]}-1; i>=0; i-- )); do
		eval "${__XYMON_TESTS_CLEANUP_CMDS[i]}" || true
	done
	exit "$rc"
}
trap __xymon_tests_run_cleanups EXIT
register_cleanup() {
	__XYMON_TESTS_CLEANUP_CMDS+=("$*")
}

# mktempdir -- create a temp dir and arrange its removal on EXIT. Prints the
# path. Callers almost always use TMP=$(mktempdir), and command substitution
# runs in a subshell: anything mktempdir appended to the cleanup array there
# would die with the subshell, leaking the dir (this is exactly what used to
# happen -- /tmp filled with xymon-test.* dirs). So rather than register a
# per-dir cleanup from inside the subshell, every dir is stamped with this
# process's PID and a single glob removal is registered once, below, in the
# parent shell at source time. `$$` is the script's PID even inside $(...),
# so the stamp is stable across the subshell boundary.
__XYMON_TESTS_TMPROOT=${TMPDIR:-/tmp}
__XYMON_TESTS_TMPROOT=${__XYMON_TESTS_TMPROOT%/}
# register_cleanup stores a string that is later eval'd, so a TMPDIR whose
# path carries whitespace or shell metacharacters would word-split or
# mis-parse at teardown -- the rm would target the wrong paths and leak the
# real dir (or worse). Shell-escape the fixed prefix with printf %q and
# append the literal glob unquoted, so the path survives eval intact while
# the trailing * still expands.
__xymon_tests_tmpglob=$(printf '%q' "${__XYMON_TESTS_TMPROOT}/xymon-test.$$.")
register_cleanup "rm -rf -- ${__xymon_tests_tmpglob}*"
mktempdir() {
	local d
	d=$(mktemp -d "${__XYMON_TESTS_TMPROOT}/xymon-test.$$.XXXXXX") || fail "mktemp -d failed"
	printf '%s' "$d"
}

# ---- repo location -----------------------------------------------------------

# find_root -- print the absolute path of the repo root, derived from the
# calling test's own location (tests/<area>/<name>.sh -> repo root is two
# dirs up). Independent of cwd, so the test produces the same result
# whether invoked from the repo top, a sibling worktree, or anywhere
# else. Prefer this over `git rev-parse --show-toplevel`, which honours
# cwd and silently picks the wrong tree when a test is launched from
# outside its own worktree.
find_root() {
	cd "$(dirname "${BASH_SOURCE[1]}")/../.." && pwd
}

# ---- binary discovery --------------------------------------------------------

# require_bin VAR DEFAULT -- ensure $VAR (or DEFAULT if unset) points to an
# executable; export VAR with the resolved path. Skip if the in-tree DEFAULT
# is absent (the binary just wasn't built in this configuration), but FAIL if
# an explicit $VAR override points at nothing: the override is the caller
# asserting the binary exists there (CMake passes the build product's path,
# autopkgtest the installed one), so a dangling path means a broken build or
# package layout -- exactly what those callers run this suite to catch -- and
# must not skip green.
#
# Usage:
#     require_bin XYMONGREP common/xymongrep
#     "$XYMONGREP" --hosts=...
#
# First consumer: tests/server/xymongrep-filter.sh. This helper is what lets
# the same test run against an in-tree build (the DEFAULT path, resolved from
# the repo root), a CMake out-of-source build, or an installed package under
# Debian autopkgtest (both export an absolute $VAR). It is also what makes the
# post-build suite run in .github/workflows/build.yml meaningful (see the note
# there): under tests.yml nothing is built, so require_bin tests skip; after a
# build they execute against the produced binary.
require_bin() {
	local var=$1 default=$2
	local cur=${!var:-}
	local explicit=$cur
	# No override: resolve the in-tree default against the repo root, not cwd.
	# A relative default like ./xymonnet/xymonping is only valid from the repo
	# top; a standalone test launched from elsewhere would otherwise probe the
	# wrong path and skip with 77 even when the binary exists. An explicit env
	# override (absolute path from CMake/autopkgtest) is used verbatim.
	if [ -z "$cur" ]; then
		case $default in
			/*) cur=$default ;;
			*)  cur=$(find_root)/$default ;;
		esac
	fi
	if [ ! -x "$cur" ]; then
		if [ -n "$explicit" ]; then
			fail "$var explicitly set to '$cur' but no executable is there -- broken build or package layout, not a skip"
		fi
		skip "$var ($cur) not built or not executable"
	fi
	# shellcheck disable=SC2163
	export "$var"="$cur"
}
