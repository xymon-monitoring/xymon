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
	(
		cd "$TMP" || exit 1
		/bin/sh "$_snippet" >/dev/null 2>"$STDERR_LOG"
	)
	printf ' %s ' "$(tr '\n' ' ' < "$_logfile")"
}
