#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/client/fs-filter-common.sh
#
# Shared scaffolding for the per-OS df/inode filesystem-filter regression
# guards (fs-filter.sh for Linux, fs-filter-freebsd.sh for FreeBSD, ...).
# Each client script implements the same XYMONCLIENT_FS_* contract but shells
# out to a different df (Linux: -x <type> per type; BSD: -t no<csv>), so the
# *assertions* are necessarily OS-specific and live in the caller. Everything
# up to "run the [df] block under a recording df stub and hand me its argv" is
# identical, and lives here.
#
# Source order in a caller:
#     . "$(dirname "$0")/../lib/assert.sh"
#     . "$(dirname "$0")/fs-filter-common.sh"
#     fsf_setup linux XYMONCLIENT_LINUX        # -> SCRIPT, TMP, STUB, *_LOG, PATH
#     ... write the df stub + decoy files (OS-specific) ...
#     fsf_extract "$TMP/df-inode-section.sh"   # [df]..[mount] minus trailing echo
#     args=$(fsf_run "$TMP/df-inode-section.sh" "$DF_LOG")
#     assert_contains ...                      # OS-specific
#
# The df stub and decoy glob files are deliberately NOT generated here: their
# routing predicate and table layout differ per OS, and threading multi-line
# heredocs through a function arg is uglier than the few lines it would save.

# fsf_setup OS_LABEL ENV_VAR
#   OS_LABEL  short uname token, e.g. linux / freebsd (only used in messages)
#   ENV_VAR   name of the autopkgtest override var, e.g. XYMONCLIENT_LINUX
#
# Resolves the client script (in-tree default, ENV_VAR points at the installed
# copy), applies the same fail-on-dangling-override / skip-if-absent contract
# as the rest of the suite, asserts the FS filter is still present (its absence
# is a regression, not a skip), and exports the working dirs/logs every caller
# needs: TMP, STUB, DF_LOG, INODE_LOG, STDERR_LOG, SCRIPT, and PATH (STUB first).
fsf_setup() {
	_os=$1
	_envvar=$2

	# Indirect-expand ${ENV_VAR:-default}. eval keeps this POSIX (no namerefs).
	eval "SCRIPT=\"\${$_envvar:-\$(find_root)/client/xymonclient-$_os.sh}\""

	if [ ! -f "$SCRIPT" ]; then
		# ENV_VAR set means the caller asserts the installed script lives
		# there; a dangling path is a broken package layout, not a green skip.
		eval "_override=\"\${$_envvar:-}\""
		[ -z "$_override" ] || fail "$_envvar set to '$SCRIPT' but no such file -- broken package layout, not a skip"
		skip "$SCRIPT missing"
	fi
	# The filter has landed, so its disappearance must FAIL (regress), not skip.
	grep -q 'XYMONCLIENT_FS_INCLUDE_TYPES' "$SCRIPT" \
		|| fail "FS filter missing from $SCRIPT (regressed)"

	TMP=$(mktempdir)
	STUB="$TMP/bin"
	mkdir -p "$STUB"
	DF_LOG="$TMP/df.args"
	INODE_LOG="$TMP/inode.args"
	STDERR_LOG="$TMP/stderr"
	export PATH="$STUB:$PATH"
}

# fsf_extract OUTFILE [SED_EXPR] [STOP]
#   Extract the report-generating block -- from `echo "[df]"` up to but not
#   including the STOP marker -- into OUTFILE so it can run in isolation under
#   the df stub. STOP defaults to the bracket-escaped `\[mount\]`, which yields
#   the combined [df]+[inode] block; the [inode] block reuses the EXCLUDES the
#   [df] block computed, so the two must be extracted together. Pass STOP as
#   `\[inode\]` to capture the [df] block alone. `sed '$d'` drops the trailing
#   `echo "$STOP"` line (portable last-line delete; `head -n -1` is GNU-only).
#   SED_EXPR, if given (may be ""), is an extra `sed -e` applied to the block --
#   Linux uses it to repoint /proc/filesystems at a fixture.
fsf_extract() {
	_out=$1
	_sed=${2:-}
	_stop=${3:-'\[mount\]'}
	if [ -n "$_sed" ]; then
		sed -n "/^echo \"\[df\]\"/,/^echo \"$_stop\"/p" "$SCRIPT" \
			| sed '$d' \
			| sed -e "$_sed" > "$_out"
	else
		sed -n "/^echo \"\[df\]\"/,/^echo \"$_stop\"/p" "$SCRIPT" \
			| sed '$d' > "$_out"
	fi
}

# fsf_wedge_binary PATH -- build the fixture the sentinel tests wedge on: a
# binary at PATH that sleeps for as many seconds as its first argument. The
# caller names it "df", because ps -o comm= answers with the interpreter for a
# script and the PID-reuse guard would then take the live fixture for a
# recycled pid (@SoundGoof).
#
# Copying the system sleep is the obvious way to get a binary of that name, and
# it works on Linux and the BSDs. It does not work everywhere: on an
# Apple-silicon Mac the kernel SIGKILLs a copy of a platform binary (rc=137,
# measured on macOS 26.5), so the wedge dies at once, the sentinel correctly
# reports a finished probe, and every wedged case fails on a fixture that never
# wedged. Compile one where there is a compiler, fall back to the copy, and --
# as asan_usable() does for the sanitizer -- prove the result runs before
# handing it back.
fsf_wedge_binary() {
	_wb=$1
	mkdir -p "$(dirname "$_wb")"
	_wbcc=${CC:-cc}
	if command -v "$_wbcc" > /dev/null 2>&1; then
		cat > "$_wb.c" <<'WEDGE'
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	sleep(argc > 1 ? (unsigned int)atoi(argv[1]) : 20);
	return 0;
}
WEDGE
		"$_wbcc" -o "$_wb" "$_wb.c" > /dev/null 2>&1 || rm -f "$_wb"
		rm -f "$_wb.c"
	fi
	if [ ! -x "$_wb" ]; then
		_wbsleep=$(command -v sleep) \
			|| skip "no C compiler and no sleep binary to build the wedge fixture from"
		cp "$_wbsleep" "$_wb" && chmod +x "$_wb" || fail "cannot create the wedge fixture"
	fi
	# A duration, not a condition: what is being waited for here is a failure
	# that shows up as the process dying immediately, and there is nothing to
	# poll for that but the passage of time.
	"$_wb" 5 &
	_wbpid=$!
	sleep 1
	if ! kill -0 "$_wbpid" 2>/dev/null; then
		skip "the wedge fixture does not stay alive on this host, so a wedged df cannot be simulated (a copied system binary is killed here)"
	fi
	kill -9 "$_wbpid" 2>/dev/null || true
	wait "$_wbpid" 2>/dev/null || true
}

# fsf_stub_helper FILE NAME BODY
#   Replace a named helper in an extracted block. The clients keep every OS
#   primitive behind such a helper -- fs_mounts, fs_filesystems, fs_procname --
#   so a test replaces the primitive the same way it replaces a command, by
#   name, instead of rewriting a path that only one OS has. The block runs as a
#   script, so the definition is edited in place rather than overridden.
fsf_stub_helper() {
	awk -v n="$2" -v b="$3" '
		$0 == n "()" || $0 == n "() {" { print n "() {"; print b; print "}"; skip = 1; next }
		skip && $0 == "{" { next }
		skip && $0 == "}" { skip = 0; next }
		skip { next }
		{ print }
	' "$1" > "$1.stubbed" && mv "$1.stubbed" "$1"
}

# fsf_run SNIPPET LOGFILE [ENV...]
#   Truncate the arg logs, run SNIPPET in a fresh subshell with $TMP as cwd
#   (so decoy glob files are in scope), and echo the recorded argv from LOGFILE
#   with newlines flattened and space-padded, so substring assertions like
#   `assert_contains " -l " ...` match at the line ends too. Per-case env vars
#   are passed inline on the command-substitution side by the caller, e.g.
#       args=$(XYMONCLIENT_FS_INCLUDE_TYPES=tmpfs fsf_run "$snip" "$DF_LOG")
#   Do NOT also set them on the outer `args=$(...)`; see the leak note in the
#   Linux test header.
fsf_run() {
	_snippet=$1
	_logfile=$2
	: > "$DF_LOG"
	: > "$INODE_LOG"
	: > "$STDERR_LOG"
	# "|| true": the block's own exit status is not part of the contract, and
	# under set -e a non-zero one would kill the caller with no message.
	(
		cd "$TMP" || exit 1
		$FSF_SHELL "$_snippet" >/dev/null 2>"$STDERR_LOG"
	) || true
	printf ' %s ' "$(tr '\n' ' ' < "$_logfile")"
}

# ---------------------------------------------------------------------------
# The contract
#
# Every client implements the same XYMONCLIENT_FS_* contract; only its spelling
# differs (Linux: -x per type, BSD: -t no<csv>, macOS: one df per path). The
# assertions below are therefore written against what the [df] and [inode]
# SECTIONS emit, not against df's argv: argv encodes the spelling, so it breaks
# whenever the mechanism changes even though the behaviour did not -- #343 had
# to rewrite `assert_not_contains " -l "` for exactly that reason, while the
# output it guarded never moved. A section-level assertion breaks only when the
# report changes, which is when unix_disk_report()/unix_inode_report() would
# break too.
#
# A caller supplies, in its own file:
#   * five ROLE variables naming filesystems the fixture must contain,
#   * five printf formats for the rows its client's post-processing expects,
#   * FSF_STUB_PARSE, a few lines of sh that turn this OS's df argv into
#     `_ex` (excluded types, space-delimited and space-padded), `_local` (non-
#     empty when only local filesystems are wanted) and `_named` (mount points
#     named as operands, macOS-style).
# Everything else -- the stub, the fixture, the runner, the assertions -- is
# here, so a rule added here is a rule every OS gets.
#
# Roles (set before fsf_write_stub):
#   FSF_LOCAL_TYPE/_MP     an ordinary local filesystem: always reported
#   FSF_PSEUDO_TYPE/_MP    a pseudo/always-full type excluded by default
#   FSF_NOINODE_TYPE/_MP   real disk usage, no inode limit: [df] yes, [inode] no
#   FSF_REMOTE_TYPE/_MP    a remote filesystem: hidden unless LOCAL_ONLY=no
#   FSF_EXTRA_TYPE/_MP     not a default anywhere, for EXCLUDE_TYPES tests
#   FSF_DECOY              a type token whose glob would match a file in $TMP

FSF_SHELL=${FSF_SHELL:-/bin/sh}

# fsf_write_stub -- write $STUB/df from the caller's roles and FSF_STUB_PARSE.
# The stub answers from the fixture, so "was this filesystem reported?" is a
# question about the client's filtering rather than about a canned table.
# The row formats take the mount point as their only argument, so a literal
# percent sign in them is written "%%" -- they end up inside a printf in the
# generated stub.
fsf_write_stub() {
	cat > "$STUB/df" <<EOF
#!/bin/sh
# Generated by fsf_write_stub() -- see tests/client/fs-filter-common.sh.
FSF_FIXTURE="$FSF_FIXTURE"
_ex=" "; _local=; _named=; _inode=
$FSF_STUB_PARSE
if [ -n "\$_inode" ]; then echo "\$*" >> "$INODE_LOG"; else echo "\$*" >> "$DF_LOG"; fi
[ -n "\${DF_FAIL:-}" ] && exit 1
# Every fixture entry the argv did not filter out. An operand that is itself
# excluded leaves df with nothing to process: it exits nonzero having printed
# nothing, which is the shape that hid the guarded probe being handed the
# exclusions for the very types it was asked to report (#343).
# In a function, not inline in the \$( ) below: bash 3.2 -- which is /bin/sh
# on macOS -- cannot parse a case statement inside a command substitution and
# dies with "syntax error near unexpected token ;;".
_pick() {
	while read -r _t _mp _isremote _noinode; do
		[ -n "\$_t" ] || continue
		case "\$_ex" in *" \$_t "*) continue ;; esac
		[ -n "\$_local" ] && [ "\$_isremote" = remote ] && continue
		if [ -n "\$_named" ]; then
			case " \$_named " in *" \$_mp "*) ;; *) continue ;; esac
		fi
		printf '%s|%s|%s\n' "\$_t" "\$_mp" "\$_noinode"
	done < "\$FSF_FIXTURE"
}
_rows=\$(_pick)
if [ -z "\$_rows" ]; then
	echo "df: no file systems processed" >&2
	exit 1
fi
if [ -n "\$_inode" ]; then
	printf '$FSF_INODE_HEADER\n'
	printf '%s\n' "\$_rows" | while IFS='|' read -r _t _mp _noinode; do
		[ -n "\$_mp" ] || continue
		if [ "\$_noinode" = noinode ]; then
			printf '$FSF_INODE_NOLIMIT_ROW\n' "\$_mp"
		else
			printf '$FSF_INODE_ROW\n' "\$_mp"
		fi
	done
	exit 0
fi
printf '$FSF_DISK_HEADER\n'
printf '%s\n' "\$_rows" | while IFS='|' read -r _t _mp _noinode; do
	[ -n "\$_mp" ] || continue
	printf '$FSF_DISK_ROW\n' "\$_mp"
done
EOF
	chmod +x "$STUB/df"
}

# fsf_write_fixture -- the mount table the stub answers from: TYPE MP REMOTE?
# NOINODE?. One line per role, so every OS exercises the same five situations.
fsf_write_fixture() {
	FSF_FIXTURE="$TMP/fsfixture"
	{
		printf '%s %s local inode\n'   "$FSF_LOCAL_TYPE"   "$FSF_LOCAL_MP"
		printf '%s %s local inode\n'   "$FSF_PSEUDO_TYPE"  "$FSF_PSEUDO_MP"
		printf '%s %s local noinode\n' "$FSF_NOINODE_TYPE" "$FSF_NOINODE_MP"
		printf '%s %s remote inode\n'  "$FSF_REMOTE_TYPE"  "$FSF_REMOTE_MP"
		printf '%s %s local inode\n'   "$FSF_EXTRA_TYPE"   "$FSF_EXTRA_MP"
		# FSF_FIXTURE_EXTRA: further "TYPE MP local|remote inode|noinode" lines
		# for filesystems an OS's own rules need -- macOS asks df about every
		# volume mount(8) lists, so each has to be answerable here.
		[ -n "${FSF_FIXTURE_EXTRA:-}" ] && printf '%s\n' "$FSF_FIXTURE_EXTRA"
	} > "$FSF_FIXTURE"
	# Every type in the fixture, so "exclude all of these" really does leave the
	# client with nothing to report.
	FSF_ALL_TYPES=$(awk '{ print $1 }' "$FSF_FIXTURE" | sort -u | tr '\n' ' ')
	export FSF_FIXTURE
}

# fsf_write_mount_stub -- a mount(8) stub rendered from the fixture, in the BSD
# spelling ("src on MP (TYPE, opts)"). A client that guards hard-blocking mounts
# reads the mount list on every DF_LOCAL_ONLY=no cycle, so without this stub the
# test would read the *tester's* mount table -- and probe whatever remote
# filesystem happens to be mounted there.
fsf_write_mount_stub() {
	# FSF_MOUNT_FMT: how this OS's mount(8) spells one line, as a printf format
	# taking MOUNTPOINT, TYPE, OPTIONS. FreeBSD and macOS put the type first
	# inside the parentheses; NetBSD and OpenBSD write "... type <fstype> (...)".
	# Getting this wrong is invisible in a stubbed test and fatal on the host.
	_fmt=${FSF_MOUNT_FMT:-'src on %s (%s, %s)\n'}
	{
		printf '#!/bin/sh\n'
		printf 'cat <<\'"'"'MOUNT'"'"'\n'
		while read -r _t _mp _isremote _rest; do
			[ -n "$_t" ] || continue
			if [ "$_isremote" = remote ]; then
				printf "$_fmt" "$_mp" "$_t" nodev
			else
				printf "$_fmt" "$_mp" "$_t" local
			fi
		done < "$FSF_FIXTURE"
		printf 'MOUNT\n'
	} > "$STUB/mount"
	chmod +x "$STUB/mount"
}

# fsf_report [ENV=VAL ...] -- run the combined [df]+[inode] block and print it.
fsf_report() {
	: > "$STDERR_LOG"; : > "$DF_LOG"; : > "$INODE_LOG"
	( cd "$TMP" && env "$@" $FSF_SHELL "$FSF_COMBINED" 2>"$STDERR_LOG" ) || true
}

# fsf_section OUTPUT NAME -- the [df] or [inode] part of a report.
fsf_section() {
	printf '%s\n' "$1" | awk -v want="[$2]" '
		$0 == want { on = 1; next }
		/^\[/ { on = 0 }
		on { print }'
}

# fsf_selfcheck -- non-vacuity. Half the contract is "X must NOT be reported",
# which a stub that ignores exclusions satisfies by reporting nothing at all,
# and an FSF_STUB_PARSE that never fills $_ex satisfies by excluding nothing.
# Probe both directions against the stub itself before trusting any of them.
fsf_selfcheck() {
	# macOS has no type filter in df: the client picks the filesystems out of
	# mount(8) and names them one per df call, so there is no exclusion spelling
	# to probe. What must not be vacuous there is the fixture -- the thing the
	# contract expects to be dropped has to be in the list to begin with.
	if [ -z "${FSF_ARGV_EXCLUDE_PSEUDO:-}" ]; then
		case "$("$STUB/mount")" in
			*"$FSF_PSEUDO_MP"*) return 0 ;;
			*) fail "stub self-check: $FSF_PSEUDO_MP is missing from the mount list, so every 'must not be reported' assertion would pass vacuously" ;;
		esac
	fi
	_plain=$(cd "$TMP" && "$STUB/df" $FSF_ARGV_PLAIN)
	case "$_plain" in
		*"$FSF_PSEUDO_MP"*) ;;
		*) fail "stub self-check: $FSF_PSEUDO_MP is missing from an unfiltered df, so every 'must not be reported' assertion would pass vacuously" ;;
	esac
	_filtered=$(cd "$TMP" && "$STUB/df" $FSF_ARGV_EXCLUDE_PSEUDO)
	case "$_filtered" in
		*"$FSF_PSEUDO_MP"*) fail "stub self-check: the stub ignores this OS's exclusion spelling ($FSF_ARGV_EXCLUDE_PSEUDO), so the filter assertions verify nothing" ;;
	esac
}

# fsf_assert_loud REPORT MSG -- the section must not read as green. Two
# spellings are equally loud and a client may use either: the "collection
# failed" marker (the column goes yellow) or one 100%-full unmeasured row
# per mount (the column goes red). What is forbidden is the third case -- an
# empty section, which the server reads as "no filesystems, all is well".
#
# The unmeasured row is matched on its columns, one line at a time: spacing
# differs per client (column-aligned vs single-spaced), and a whole-report
# glob took dashes in device names plus any 100% row for the marker.
fsf_assert_loud() {
	case "$1" in
		*"collection failed"*) return 0 ;;
	esac
	printf '%s\n' "$1" | grep -Eq '^[^ ]+ +- +- +- +100% ' && return 0
	fail "${2:-}: the report is silent, which the server reads as green: '$1'"
}

# fsf_contract -- the rules every client obeys, asserted on the emitted report.
fsf_contract() {
	_out=$(fsf_report)
	_df=$(fsf_section "$_out" df)
	_in=$(fsf_section "$_out" inode)

	assert_contains "$FSF_LOCAL_MP" "$_df" \
		"contract: an ordinary local filesystem must be reported (block stderr: $(tr '\n' ' ' < "$STDERR_LOG" | cut -c1-300))"
	assert_not_contains "$FSF_PSEUDO_MP" "$_df" \
		"contract: $FSF_PSEUDO_TYPE is excluded by default"
	assert_not_contains "$FSF_REMOTE_MP" "$_df" \
		"contract: a remote filesystem is hidden unless DF_LOCAL_ONLY=no"

	# INCLUDE_TYPES un-excludes; the token is literal, never a glob, even with a
	# matching filename in the working directory.
	_out=$(fsf_report "XYMONCLIENT_FS_INCLUDE_TYPES=$FSF_PSEUDO_TYPE")
	assert_contains "$FSF_PSEUDO_MP" "$(fsf_section "$_out" df)" \
		"contract: INCLUDE_TYPES un-excludes a default-excluded type"
	_out=$(fsf_report "XYMONCLIENT_FS_INCLUDE_TYPES=$FSF_DECOY")
	assert_not_contains "$FSF_PSEUDO_MP" "$(fsf_section "$_out" df)" \
		"contract: an INCLUDE_TYPES token must not undergo pathname expansion"

	# EXCLUDE_TYPES adds, wins over INCLUDE_TYPES, and is literal too.
	_out=$(fsf_report "XYMONCLIENT_FS_EXCLUDE_TYPES=$FSF_EXTRA_TYPE")
	assert_not_contains "$FSF_EXTRA_MP" "$(fsf_section "$_out" df)" \
		"contract: EXCLUDE_TYPES drops a type that was being reported"
	assert_contains "$FSF_LOCAL_MP" "$(fsf_section "$_out" df)" \
		"contract: EXCLUDE_TYPES must not take other filesystems with it"
	_out=$(fsf_report "XYMONCLIENT_FS_INCLUDE_TYPES=$FSF_EXTRA_TYPE" \
			"XYMONCLIENT_FS_EXCLUDE_TYPES=$FSF_EXTRA_TYPE")
	assert_not_contains "$FSF_EXTRA_MP" "$(fsf_section "$_out" df)" \
		"contract: a type in both lists stays excluded (exclude wins)"
	# ... and an EXCLUDE token is as literal as an INCLUDE one. The glob is the
	# type with its last character replaced by "*", and a file of the exact type
	# name sits in the working directory, so a client that lost its "set -f"
	# expands the token to that name and drops a filesystem nobody excluded.
	: > "$TMP/$FSF_EXTRA_TYPE"
	_out=$(fsf_report "XYMONCLIENT_FS_EXCLUDE_TYPES=${FSF_EXTRA_TYPE%?}*")
	assert_contains "$FSF_EXTRA_MP" "$(fsf_section "$_out" df)" \
		"contract: an EXCLUDE_TYPES token must not undergo pathname expansion"

	# More than one token per list, and a token that is already excluded. Both
	# are where a "-t no<csv>" client assembles its list wrong -- a lost token,
	# a doubled entry -- and neither may change what is reported.
	_out=$(fsf_report "XYMONCLIENT_FS_INCLUDE_TYPES=$FSF_PSEUDO_TYPE $FSF_EXTRA_TYPE")
	_df=$(fsf_section "$_out" df)
	assert_contains "$FSF_PSEUDO_MP" "$_df" \
		"contract: the first of two INCLUDE_TYPES tokens takes effect"
	assert_contains "$FSF_EXTRA_MP" "$_df" \
		"contract: the second of two INCLUDE_TYPES tokens takes effect"
	# Naming a type that is already excluded must be a no-op for what is
	# reported. Deliberately not a whole-output comparison: on macOS the default
	# drops carry the "root data" exemption while an explicit EXCLUDE_TYPES is
	# unconditional, so the two runs legitimately differ elsewhere.
	_out=$(fsf_report "XYMONCLIENT_FS_EXCLUDE_TYPES=$FSF_PSEUDO_TYPE")
	_df=$(fsf_section "$_out" df)
	assert_not_contains "$FSF_PSEUDO_MP" "$_df" \
		"contract: excluding an already-excluded type keeps it excluded"
	assert_contains "$FSF_LOCAL_MP" "$_df" \
		"contract: ... and takes nothing else with it"

	# DF_LOCAL_ONLY=no is what surfaces a remote filesystem.
	_out=$(fsf_report "XYMONCLIENT_FS_DF_LOCAL_ONLY=no")
	assert_contains "$FSF_REMOTE_MP" "$(fsf_section "$_out" df)" \
		"contract: DF_LOCAL_ONLY=no reports a remote filesystem"
	assert_contains "$FSF_LOCAL_MP" "$(fsf_section "$_out" df)" \
		"contract: DF_LOCAL_ONLY=no keeps the local ones too"

	# An invalid DF_LOCAL_ONLY warns and fails safe: remote stays hidden.
	_out=$(fsf_report "XYMONCLIENT_FS_DF_LOCAL_ONLY=bogus")
	assert_not_contains "$FSF_REMOTE_MP" "$(fsf_section "$_out" df)" \
		"contract: an invalid DF_LOCAL_ONLY falls back to local-only"
	assert_contains "invalid XYMONCLIENT_FS_DF_LOCAL_ONLY" "$(cat "$STDERR_LOG")" \
		"contract: an invalid DF_LOCAL_ONLY warns on stderr"

	# The inode report is filtered exactly like the disk report, and drops the
	# filesystems that cannot run out of inodes while keeping their disk rows.
	_out=$(fsf_report)
	_df=$(fsf_section "$_out" df); _in=$(fsf_section "$_out" inode)
	assert_contains "$FSF_LOCAL_MP" "$_in" \
		"contract: the inode report keeps a filesystem with real inode accounting"
	assert_not_contains "$FSF_PSEUDO_MP" "$_in" \
		"contract: the inode report applies the same exclusions as [df]"
	assert_not_contains "$FSF_NOINODE_MP" "$_in" \
		"contract: a filesystem with no inode limit is dropped from [inode]"
	assert_contains "$FSF_NOINODE_MP" "$_df" \
		"contract: ... while its disk row, which is a real percentage, stays"
	_out=$(fsf_report "XYMONCLIENT_FS_EXCLUDE_TYPES=$FSF_EXTRA_TYPE")
	assert_not_contains "$FSF_EXTRA_MP" "$(fsf_section "$_out" inode)" \
		"contract: EXCLUDE_TYPES reaches the inode report too"

	# A df that fails, and a filter that leaves nothing, must both be loud: the
	# server reads an absent filesystem as green, so silence is a false OK.
	_out=$(fsf_report DF_FAIL=1)
	fsf_assert_loud "$_out" \
		"contract: a df that exits nonzero with no output must be loud"
	assert_not_contains "Filesystem" "$_out" \
		"contract: the failure marker carries no df header (a header reads as a healthy report)"
	# ... including where the collection is split in two. A client that runs one
	# df for the local set and probes remote mounts separately has a second way
	# to lose the marker: swallow the plain df's status, and a failure with no
	# rows becomes an empty section, which the server reads as green.
	_out=$(fsf_report DF_FAIL=1 XYMONCLIENT_FS_DF_LOCAL_ONLY=no)
	fsf_assert_loud "$_out" \
		"contract: a df failure stays loud with DF_LOCAL_ONLY=no"
	_out=$(fsf_report "XYMONCLIENT_FS_EXCLUDE_TYPES=$FSF_ALL_TYPES")
	fsf_assert_loud "$_out" \
		"contract: excluding every filesystem must be loud, never a silent empty section"
}
