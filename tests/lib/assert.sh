# SPDX-License-Identifier: GPL-2.0-or-later
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

# mktempdir -- create a temp dir and register its removal. Prints the path.
mktempdir() {
	local d
	d=$(mktemp -d -t "xymon-test.XXXXXX") || fail "mktemp -d failed"
	register_cleanup "rm -rf '$d'"
	printf '%s' "$d"
}

# ---- binary discovery --------------------------------------------------------

# require_bin VAR DEFAULT -- ensure $VAR (or DEFAULT if unset) points to an
# executable; export VAR with the resolved path. Skip if absent so tests
# don't fail when the binary just wasn't built in this configuration.
#
# Usage:
#     require_bin XYMONPING ./xymonnet/xymonping
#     "$XYMONPING" --help
require_bin() {
	local var=$1 default=$2
	local cur=${!var:-$default}
	if [ ! -x "$cur" ]; then
		skip "$var ($cur) not built or not executable"
	fi
	# shellcheck disable=SC2163
	export "$var"="$cur"
}
