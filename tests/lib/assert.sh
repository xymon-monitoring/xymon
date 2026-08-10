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

# ---- C harness scaffolding ---------------------------------------------------

# require_cc -- skip unless a C compiler is present. Sets/keeps CC (default
# cc). For tests that compile a standalone harness with no in-tree libraries.
require_cc() {
	CC=${CC:-cc}
	command -v "$CC" >/dev/null 2>&1 || skip "no C compiler available (CC=$CC)"
}

# require_c_buildenv ROOT -- skip unless a C compiler, make, and a configured
# tree (include/config.h) are present. The shared baseline for every test
# that compiles a harness against the in-tree libraries, so a new shared
# skip condition lands in one place; a test may still add stricter
# conditions of its own (e.g. "tree already built") next to its call.
require_c_buildenv() {
	require_cc
	command -v make >/dev/null 2>&1 || skip "make not available"
	[ -f "$1/include/config.h" ] || skip "tree not configured (no include/config.h)"
}

# build_xymon_libs ROOT LOGFILE ARCHIVE... -- build the named archives in
# ROOT/lib, dumping the build log on stderr and failing if the build breaks.
build_xymon_libs() {
	local root=$1 log=$2
	shift 2
	make -C "$root/lib" "$@" >"$log" 2>&1 \
		|| { cat "$log" >&2; fail "cannot build $*"; }
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

# ---- build-variant manifest --------------------------------------------------

# A build variant is a promise about which programs the build produces, and the
# promises nest. A server build compiles the whole client tool set as well
# (client/xymon, client/xymongrep, ...) on top of xymond/, web/, xymonnet/ and
# xymongen/; a localclient build is a client build plus the local data analyser
# client/xymond_client, which is a server capability running locally. So:
#
#   capability    provided by
#   client        server, localclient, client
#   localclient   server, localclient
#   server        server
#
# Stating the containment once, here, is what makes "should this test run in
# this build?" answerable rather than guessed.
variant_capabilities() {
	cat <<-'EOF'
		client       server localclient client
		localclient  server localclient
		server       server
	EOF
}

# The build products a test may ask for, by role: the capability the role
# belongs to, then where each providing variant puts it.
#
# The capability column is not decoration -- check_role_manifest verifies it
# against the paths. Every variant providing the capability must have a path
# here, so forgetting one (the way a new client-capability role would silently
# read as "localclient does not build it") is a loud table error rather than a
# wrong skip.
#
# Deliberately partial: only products a variant builds *unconditionally*. RRD-
# and web-dependent ones (xymond_rrd, svcstatus.cgi, showgraph.cgi) turn on
# build options rather than on the variant, so listing them would false-fail a
# legitimate --server build made without RRD; those keep their own probe-and-
# skip. A server build carries both common/xymongrep and client/xymongrep; the
# server row names common/, which is what the server package ships.
role_manifest() {
	cat <<-'EOF'
		XYMONGREP      client       server=common/xymongrep localclient=client/xymongrep client=client/xymongrep
		XYMOND_CLIENT  localclient  server=xymond/xymond_client localclient=client/xymond_client
	EOF
}

# role_capability ROLE -- the capability ROLE belongs to, empty if the manifest
# says nothing about ROLE (a build-option-dependent product: probe instead).
role_capability() {
	role_manifest | awk -v r="$1" '$1==r {print $2}'
}

# capability_providers CAP -- the variants providing CAP, one per line, sorted.
capability_providers() {
	variant_capabilities | awk -v c="$1" '$1==c {for (i=2;i<=NF;i++) print $i}' | sort
}

# role_variants ROLE -- the variants the manifest gives ROLE a path for, sorted.
role_variants() {
	role_manifest | awk -v r="$1" '$1==r {for (i=3;i<=NF;i++) {split($i,kv,"="); print kv[1]}}' | sort
}

# role_path ROLE VARIANT -- where VARIANT builds ROLE, empty if it does not.
role_path() {
	role_manifest | awk -v r="$1" -v v="$2" \
		'$1==r {for (i=3;i<=NF;i++) {split($i,kv,"="); if (kv[1]==v) print kv[2]}}'
}

# role_paths ROLE -- every path any variant builds ROLE at. Used when no variant
# is declared, so one test still finds the product wherever the local build put
# it, without each test hand-rolling its own probe.
role_paths() {
	role_manifest | awk -v r="$1" '$1==r {for (i=3;i<=NF;i++) {split($i,kv,"="); print kv[2]}}'
}

# check_role_manifest ROLE -- the two tables must agree: a role declared as a
# CAP capability needs a path for exactly the variants providing CAP. A missing
# path would otherwise read as "this variant does not build it" and skip green,
# which is the failure mode the manifest exists to remove.
check_role_manifest() {
	local role=$1 cap want got
	cap=$(role_capability "$role") || return 0
	[ -n "$cap" ] || return 0
	want=$(capability_providers "$cap")
	got=$(role_variants "$role")
	[ -n "$want" ] || fail "manifest: role $role declares capability '$cap', which no variant provides -- table bug"
	[ "$want" = "$got" ] && return 0
	fail "manifest: role $role is a '$cap' capability, provided by [$(echo "$want" | tr '\n' ' ')] but given paths for [$(echo "$got" | tr '\n' ' ')] -- table bug"
}

# check_variant -- a non-empty $XYMON_VARIANT naming no real variant would match
# no manifest row, so every promise would silently evaporate and the lane go
# green having verified nothing. A CI-matrix typo must fail, not skip.
check_variant() {
	case "${XYMON_VARIANT:-}" in
		''|server|client|localclient) ;;
		*) fail "XYMON_VARIANT='${XYMON_VARIANT}' is not a known variant (server|client|localclient) -- likely a CI-matrix typo; refusing to skip silently" ;;
	esac
}

# ---- build-product discovery -------------------------------------------------

# require_bin VAR DEFAULT -- ensure $VAR points to an executable build product;
# export VAR with the resolved path. Resolution order:
#
#   1. an explicit $VAR (CMake out-of-source, autopkgtest) -- used verbatim, and
#      a dangling path FAILS: the override is the caller asserting the binary is
#      there, so a broken build or package layout must not skip green;
#   2. $XYMON_VARIANT set and VAR is a manifest role -- the path this variant
#      builds it at; fail if that is missing, and skip with the capability as
#      the stated reason if this variant does not provide VAR at all;
#   3. otherwise -- probe every path the manifest associates with VAR, then
#      DEFAULT, and skip if none exist.
#
# Usage:
#     require_bin XYMONGREP common/xymongrep
#     "$XYMONGREP" --hosts=...
#
# Rule 3 is what lets a single test run against whichever variant the developer
# happens to have built, without each test hand-rolling its own path probe --
# the per-variant paths live in the manifest, once.
require_bin() {
	local var=$1 default=$2
	local cur=${!var:-}
	local explicit=$cur
	local rel
	check_variant
	# No override: resolve the in-tree path against the repo root, not cwd. A
	# relative path like ./xymonnet/xymonping is only valid from the repo top; a
	# standalone test launched from elsewhere would otherwise probe the wrong
	# path and skip with 77 even when the file exists. An explicit env override
	# (absolute path from CMake/autopkgtest) is used verbatim and outranks the
	# manifest -- the caller is asserting where the product is.
	if [ -z "$cur" ]; then
		local cap
		cap=$(role_capability "$var")
		if [ -n "${XYMON_VARIANT:-}" ] && [ -n "$cap" ]; then
			check_role_manifest "$var"
			rel=$(role_path "$var" "$XYMON_VARIANT")
			# This variant does not provide the capability the role belongs
			# to, so the build legitimately has no such program. A declared
			# skip, not a guess about what "$XYMON_VARIANT" contains.
			[ -n "$rel" ] \
				|| skip "the ${XYMON_VARIANT} build does not provide the '$cap' capability ($var)"
		else
			# No manifest row for this role (a build-option-dependent product,
			# or an unbuilt tree that promises nothing): probe every path any
			# variant is known to use, then the caller's default.
			for rel in $(role_paths "$var") "$default"; do
				case $rel in
					/*) [ -x "$rel" ] && break ;;
					*)  [ -x "$(find_root)/$rel" ] && break ;;
				esac
			done
		fi
		case $rel in
			/*) cur=$rel ;;
			*)  cur=$(find_root)/$rel ;;
		esac
	fi
	if [ ! -x "$cur" ]; then
		if [ -n "$explicit" ]; then
			fail "$var explicitly set to '$cur' but no executable is there -- broken build or package layout, not a skip"
		fi
		# The variant promised this file and the build did not produce it. That
		# is a build regression, and skipping green is precisely how it would go
		# unnoticed -- which is the whole point of having a manifest.
		if [ -n "${XYMON_VARIANT:-}" ] && [ -n "$(role_path "$var" "$XYMON_VARIANT")" ]; then
			fail "the ${XYMON_VARIANT} build is defined to produce $var at '$cur', but it is not built or not executable -- build regression, not a skip"
		fi
		skip "$var ($cur) not built or not executable"
	fi
	# shellcheck disable=SC2163
	export "$var"="$cur"
}
