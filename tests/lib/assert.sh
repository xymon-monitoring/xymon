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

# assert_not_match REGEX STRING [MSG] -- the negation of assert_match
assert_not_match() {
	local re=$1 s=$2 msg=${3:-}
	if [[ $s =~ $re ]]; then
		fail "${msg:+$msg: }'$s' matches /$re/"
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
# happen -- /tmp filled with xt.* dirs). So rather than register a
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
__xymon_tests_tmpglob=$(printf '%q' "${__XYMON_TESTS_TMPROOT}/xt.$$.")
register_cleanup "rm -rf -- ${__xymon_tests_tmpglob}*"
mktempdir() {
	local d
	# "xt", not "xymon-test". This path becomes $XYMONTMP, and a test that
	# starts xymond_rrd puts its cache-control socket under it -- which has to
	# fit in sizeof(sun_path), 104 bytes on macOS. The runner nests this
	# directory inside a per-run root, so on macOS, where TMPDIR is already a
	# 49-character /var/folders path, the longer name put XYMONTMP at 93 bytes
	# against a limit of 90 and the socket could not be bound. Six tests failed
	# on it, and only in a full run: run one alone and the per-run level is not
	# there. CI never saw it -- Linux allows 108 bytes and TMPDIR is /tmp.
	d=$(mktemp -d "${__XYMON_TESTS_TMPROOT}/xt.$$.XXXXXX") || fail "mktemp -d failed"
	printf '%s' "$d"
}

# ---- C harness scaffolding ---------------------------------------------------

# require_cc -- skip unless a C compiler is present. Sets/keeps CC (default
# cc). For tests that compile a standalone harness with no in-tree libraries.
require_cc() {
	CC=${CC:-cc}
	command -v "$CC" >/dev/null 2>&1 || skip "no C compiler available (CC=$CC)"
}

# asan_usable -- true when $CC can build a sanitized binary AND the result
# runs. Compiling is not the question: distros that ship the sanitizer
# runtime in its own package (Amazon Linux 2023 and the other RPM families,
# where it is libasan/libubsan) link -fsanitize=address happily, and only
# the finished binary fails, at startup, with
#
#     error while loading shared libraries: libasan.so.6: ...
#
# and exit 127. A caller that gated on the compile alone then reads that as
# a crash in the code under test -- the symptom this helper exists to stop.
# So the probe is executed, not just built.
#
# Callers use it to pick between a sanitized build and a plain one, or to
# skip outright when the sanitizer is the point of the test. The verdict
# cannot change inside one test process, so it is computed once.
#
# The probe directory is removed here rather than left to the EXIT trap
# registered at the top of this file: several callers install a trap of
# their own for their work dir, which replaces that one, and the probe
# would then survive the run. A helper cannot assume the shared cleanup is
# still armed by the time it is called, so it cleans up after itself.
asan_usable() {
	if [ -z "${__xymon_tests_asan_usable:-}" ]; then
		local probe
		__xymon_tests_asan_usable=no
		probe=$(mktempdir)
		printf 'int main(void){return 0;}\n' >"$probe/probe.c"
		if "${CC:-cc}" -fsanitize=address,undefined -o "$probe/probe" \
					"$probe/probe.c" 2>/dev/null \
				&& "$probe/probe" >/dev/null 2>&1; then
			__xymon_tests_asan_usable=yes
		fi
		rm -rf "$probe"
	fi
	[ "$__xymon_tests_asan_usable" = yes ]
}

# require_gnu_make -- resolve GNU make into $XYMON_MAKE, or skip.
#
# Every Makefile in the tree is GNU make: lib/Makefile opens with an `ifeq`,
# build/Makefile.rules uses pattern rules. On the BSDs /usr/bin/make is BSD
# make, which stops at the first conditional --
#
#     make: lib/Makefile:10: Invalid line "ifeq ($(LOCALCLIENT),yes)"
#
# -- so a test that ran `make` there hard-failed on a tree that builds
# perfectly well with gmake, which is what the build itself uses.
#
# Resolution order: $MAKE if the caller exported one (autopkgtest, a distro
# build), then gmake, then make. Each candidate must identify itself as GNU
# make; a candidate that exists but is not GNU make is passed over rather
# than trusted, because "a program named make is on PATH" is the assumption
# that produced the failure above.
#
# No GNU make at all is a missing host dependency, so it skips -- same
# contract as a missing compiler (tests/README.md). Resolved once: PATH
# cannot change inside one test process.
require_gnu_make() {
	[ -n "${XYMON_MAKE:-}" ] && return 0
	local candidate
	for candidate in ${MAKE:-} gmake make; do
		command -v "$candidate" >/dev/null 2>&1 || continue
		# Matched on the whole banner rather than `--version | head -1`:
		# every test runs under `set -o pipefail`, and head exiting after
		# the first line can SIGPIPE the writer, failing the pipeline for
		# a candidate that answered correctly.
		case $("$candidate" --version 2>/dev/null) in
			"GNU Make"*) XYMON_MAKE=$candidate; return 0 ;;
		esac
	done
	skip "GNU make not found (the tree's Makefiles are GNU make -- install gmake)"
}

# require_c_buildenv ROOT -- skip unless a C compiler, GNU make, and a
# configured tree (include/config.h) are present. The shared baseline for every
# test that compiles a harness against the in-tree libraries, so a new shared
# skip condition lands in one place; a test may still add stricter
# conditions of its own (e.g. "tree already built") next to its call.
require_c_buildenv() {
	require_cc
	require_gnu_make
	[ -f "$1/include/config.h" ] || skip "tree not configured (no include/config.h)"
}

# build_xymon_libs ROOT LOGFILE ARCHIVE... -- build the named archives in
# ROOT/lib, dumping the build log on stderr and failing if the build breaks.
build_xymon_libs() {
	local root=$1 log=$2
	shift 2
	require_gnu_make
	"$XYMON_MAKE" -C "$root/lib" "$@" >"$log" 2>&1 \
		|| { cat "$log" >&2; fail "cannot build $*"; }
}

# pcre_cflags ROOT -- print the compile flags a harness needs to reach
# <pcre2.h>, or nothing when the header is already on the default search
# path.
#
# Every harness that includes libxymon.h needs this whether or not it uses
# PCRE: libxymon.h pulls in lib/loadalerts.h, which includes <pcre2.h>. The
# harnesses resolved the PCRE *libraries* and assumed the *header*, which
# holds on Linux (/usr/include/pcre2.h) and fails wherever PCRE lives under
# a prefix -- on FreeBSD, /usr/local/include:
#
#     lib/loadalerts.h:20:10: fatal error: 'pcre2.h' file not found
#
# Resolution mirrors the PCRELIBS order already used for the link, most
# specific first: an explicit override, then the configured tree (whose
# PCREINCDIR is by definition what the library being linked was built
# against, and already carries its -I), then pkg-config, then nothing.
pcre_cflags() {
	local root=$1 flags=${PCRECFLAGS:-}
	[ -n "$flags" ] || [ ! -f "$root/Makefile" ] \
		|| flags=$(sed -n 's/^PCREINCDIR *= *//p' "$root/Makefile")
	if [ -z "$flags" ] && command -v pkg-config >/dev/null 2>&1; then
		flags=$(pkg-config --cflags libpcre2-8 2>/dev/null || true)
	fi
	printf '%s' "$flags"
}

# xymon_cflags ROOT -- the include flags a harness compiled against the
# in-tree libraries needs: the tree's own headers, and wherever <pcre2.h>
# lives. Callers add whatever else they need after it (the harness's own
# -iquote ROOT/web or ROOT/xymond, -DSTANDALONE, the RRD defines).
#
# The tree's directories go on the *quoted* path and the PCRE directory on
# the angle-bracket one, because that is how each is included: every in-tree
# header is reached through #include "...", while loadalerts.h asks for
# <pcre2.h>. Putting the tree on the angle-bracket path is what makes
# lib/availability.h answer the macOS SDK's #include <Availability.h> on a
# case-insensitive filesystem (#328).
#
# Bundled into one helper rather than offered as separate flags on purpose.
# Neither mistake here was a wrong flag: each was a flag that every harness
# had to remember for itself. The PCRE path had to be remembered fourteen
# times and eleven compile lines did not remember it; -iquote has to be
# remembered at forty-two flags across nineteen files, and the next harness
# written will have to remember it again. A compile line can no longer say
# where the tree's headers are without also saying how to reach them and
# where pcre2.h is.
xymon_cflags() {
	local root=$1
	printf '%s' "-iquote $root/include -iquote $root/lib $(pcre_cflags "$root")"
}

# xymon_ldflags ROOT -- the configured link flags for a harness built against
# the in-tree libraries: the library search path, the runtime path, and the
# libraries libxymoncomm.a itself needs (XYMONCOMMLIBS in xymond/Makefile).
#
# The counterpart to xymon_cflags(), and bundled for the same reason: the
# product build gets all of this from $(CFLAGS) and $(RPATHOPT) on its link
# line, while each harness spells its own and so far none carried any of it.
# On the BSDs that costs a link (`ld: cannot find -lpcre2-8`, because
# build/Makefile.FreeBSD keeps -L/usr/local/lib inside CFLAGS) or a run
# (`Shared object "libpcre2-8.so.0" not found`, no rpath).
#
# Asked of make rather than sed'd out of the Makefile: NETLIBS, ZLIBLIBS and
# LIBRTDEF live in the per-OS file the toplevel Makefile includes, and the
# search path has no variable of its own at all - it is embedded in CFLAGS,
# so the -L tokens are picked out of it.
xymon_ldflags() {
	local root=$1 probe out cflags rpath libs tok search=

	[ -f "$root/Makefile" ] || return 0
	require_gnu_make
	# shellcheck disable=SC2016  # $(CFLAGS) etc. are make's to expand, not the shell's
	probe='__xymon_ldflags:
	@printf "%s\n" "$(CFLAGS)" "$(RPATHOPT)" "$(ZLIBLIBS) $(SSLLIBS) $(NETLIBS) $(LIBRTDEF)"
'
	out=$(printf '%s' "$probe" | "$XYMON_MAKE" -s -C "$root" -f Makefile -f - __xymon_ldflags 2>/dev/null) || return 0

	cflags=$(printf '%s\n' "$out" | sed -n 1p)
	rpath=$(printf '%s\n' "$out" | sed -n 2p)
	libs=$(printf '%s\n' "$out" | sed -n 3p)

	# Only the search path is wanted from CFLAGS; its -I/-D/-W belong to the
	# compile side, which xymon_cflags() already covers.
	for tok in $cflags; do
		case $tok in -L*) search="$search $tok" ;; esac
	done

	# RPATHOPT is $(RPATH)$(RPATHVAL); with no RPATHVAL configured it is a bare
	# "-Wl,--rpath," with nothing after the comma, so drop it rather than hand
	# the linker a dangling flag.
	case $rpath in *,) rpath= ;; esac

	printf '%s' "$search $rpath $libs"
}

# require_shm_segments N -- skip unless one process may attach N SysV
# shared-memory segments. xymond attaches one per channel, and macOS ships
# kern.sysv.shmseg=8 against the nine channels xymond sets up, so a stock Mac
# cannot start xymond at all.
#
# Worth a named check rather than letting the run fail: shmat() reports the
# limit as EMFILE, which strerror() renders "Too many open files", so the
# error points at file descriptors and says nothing about the sysctl actually
# responsible. The skip names the knob and the value so it is actionable.
#
# Linux has no comparable per-process cap (SHMSEG is effectively unlimited)
# and exposes no such sysctl, so an absent knob means "no limit to check".
require_shm_segments() {
	local want=$1 have knob found=

	for knob in kern.sysv.shmseg kern.ipc.shmseg; do
		have=$(sysctl -n "$knob" 2>/dev/null) || continue
		case $have in ''|*[!0-9]*) continue ;; esac
		found=$knob
		break
	done
	[ -n "$found" ] || return 0
	[ "$have" -ge "$want" ] && return 0

	skip "$found is $have, need >= $want (xymond attaches one shared-memory segment per channel) -- raise it with: sudo sysctl -w $found=$want"
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

# require_cfg VAR DEFAULT -- ensure $VAR (or DEFAULT if unset) points to a
# config file; export VAR with the resolved path. Same override contract as
# require_bin: a missing in-tree default is a skip, but a missing explicit
# override is a failure because the caller asserted that installed layout.
require_cfg() {
	local var=$1 default=$2
	local cur=${!var:-}
	local explicit=$cur
	if [ -z "$cur" ]; then
		case $default in
			/*) cur=$default ;;
			*)  cur=$(find_root)/$default ;;
		esac
	fi
	if [ ! -f "$cur" ]; then
		if [ -n "$explicit" ]; then
			fail "$var explicitly set to '$cur' but no file is there -- broken build or package layout, not a skip"
		fi
		skip "$var ($cur) not found"
	fi
	# shellcheck disable=SC2163
	export "$var"="$cur"
}
