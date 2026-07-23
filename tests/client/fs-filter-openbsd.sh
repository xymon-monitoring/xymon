#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/client/fs-filter-openbsd.sh
#
# Regression guard for the df/inode filesystem filter ported to
# xymonclient-openbsd.sh (issue #170): XYMONCLIENT_FS_{INCLUDE,EXCLUDE}_TYPES
# via df "-t no<csv>", df -l (XYMONCLIENT_FS_DF_LOCAL_ONLY), and the inode "-"
# (no inode limit) row drop.
#
# Mock-tested on any host (df-argv construction). Real OpenBSD df still needs
# verifying on OpenBSD.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
# shellcheck source=tests/client/fs-filter-common.sh
. "$(dirname "$0")/fs-filter-common.sh"

# Resolve SCRIPT, apply the dangling-override / skip-if-absent contract, assert
# the filter is present, set up TMP/STUB/DF_LOG/INODE_LOG/STDERR_LOG/PATH.
fsf_setup openbsd XYMONCLIENT_OPENBSD

cat > "$STUB/df" <<EOF
#!/usr/bin/env bash
# DF_FAIL=1 simulates a df that exits non-zero with no output (e.g. killed).
[ -n "\${DF_FAIL:-}" ] && exit 1
if [[ " \$* " =~ " -i " ]]; then
	echo "\$*" >> "$INODE_LOG"
	printf 'Filesystem 1K-blocks Used Avail Capacity iused ifree %%iused Mounted on\n'
	printf '/dev/sd0a 100 50 50 50%% 1000 9000 10%% /\n'
	printf 'nolimitfs 200 1 199 1%% 0 0 - /nolimit\n'
else
	echo "\$*" >> "$DF_LOG"
	printf 'Filesystem 1K-blocks Used Avail Capacity Mounted on\n'
	printf '/dev/sd0a 100 50 50 50%% /\n'
fi
EOF
chmod +x "$STUB/df"

# Decoy files in the snippet's working directory ($TMP): if the script let a
# configured type token glob, "procf*"/"fuse.*" would expand to these filenames.
: > "$TMP/procfs"
: > "$TMP/fuse.sshfs"

SNIPPET="$TMP/df-section.sh"
fsf_extract "$SNIPPET"
run() { fsf_run "$SNIPPET" "$DF_LOG"; }

# --- default ----------------------------------------------------------------
args=$(run)
assert_contains " -k " "$args" "disk df keeps -k"
assert_contains " -l " "$args" "default is local-only (df -l)"
assert_contains " -tnokernfs,procfs,cd9660 " "$args" \
	"default excludes pseudo types via -t no<csv>"
assert_not_contains "nonfs" "$args" "nfs is hidden by df -l, not by the type list"

inode_args=$(printf ' %s ' "$(tr '\n' ' ' < "$INODE_LOG")")
assert_contains " -i " "$inode_args" "inode df uses -i"
# tmpfs stays in both reports (unlike FreeBSD): OpenBSD's tmpfs inode counts
# are memory-derived (real signal), not FreeBSD's INT_MAX sentinel.
assert_not_contains "tmpfs" "$args"       "the disk report keeps tmpfs"
assert_not_contains "tmpfs" "$inode_args" "the inode report keeps tmpfs (memory-derived counts)"

# --- INCLUDE / EXCLUDE / DF_LOCAL_ONLY --------------------------------------
args=$(XYMONCLIENT_FS_INCLUDE_TYPES=kernfs run)
assert_not_contains "kernfs" "$args" "INCLUDE_TYPES=kernfs un-excludes kernfs"
args=$(XYMONCLIENT_FS_EXCLUDE_TYPES=ext2fs run)
assert_contains "ext2fs" "$args" "EXCLUDE_TYPES=ext2fs adds ext2fs"
args=$(XYMONCLIENT_FS_DF_LOCAL_ONLY=no run)
assert_not_contains " -l " "$args" "DF_LOCAL_ONLY=no drops -l"
args=$(XYMONCLIENT_FS_DF_LOCAL_ONLY=bogus run)
assert_contains " -l " "$args" "invalid DF_LOCAL_ONLY falls back to local-only"
assert_contains "invalid XYMONCLIENT_FS_DF_LOCAL_ONLY" "$(cat "$STDERR_LOG")" \
	"invalid DF_LOCAL_ONLY warns"

# Type tokens are literal, not shell globs. With $TMP as the working directory,
# "procf*" must not expand to the decoy file and accidentally un-exclude procfs.
args=$(XYMONCLIENT_FS_INCLUDE_TYPES='procf*' run)
assert_contains ",procfs," "$args" \
	"include type tokens must not undergo pathname expansion"

# Likewise an exclude glob must stay literal, not become the decoy filename.
args=$(XYMONCLIENT_FS_EXCLUDE_TYPES='fuse.*' run)
assert_contains "fuse.*" "$args" \
	"exclude type tokens must not undergo pathname expansion"
assert_not_contains "fuse.sshfs" "$args" \
	"exclude type glob must not expand to a working-directory filename"

# --- include + exclude precedence: exclude wins -----------------------------
# A type named in both lists stays excluded: EXCLUDE is applied last (documented
# contract). ext2fs is not a default, so its presence is unambiguous.
args=$(XYMONCLIENT_FS_INCLUDE_TYPES=ext2fs XYMONCLIENT_FS_EXCLUDE_TYPES=ext2fs run)
assert_contains "ext2fs" "$args" \
	"a type in both include and exclude lists must stay excluded (exclude wins)"

# --- inode "-" (no inode limit) rows are dropped ----------------------------
out=$( cd "$TMP"; /bin/sh "$SNIPPET" 2>/dev/null )
inode_section=$(printf '%s\n' "$out" | sed -n '/^\[inode\]/,$p')
assert_not_contains "/nolimit" "$inode_section" \
	"inode report drops the no-inode-limit filesystem (%iused '-')"

# --- false-green guard: df fails with no output -> marker, not empty section --
out=$( cd "$TMP"; DF_FAIL=1 /bin/sh "$SNIPPET" 2>/dev/null )
assert_contains "Disk report collection failed" "$out" \
	"a failed disk df emits a failure marker"
assert_contains "Inode report collection failed" "$out" \
	"a failed inode df emits a failure marker"
assert_not_contains "Filesystem" "$out" \
	"failure marker carries no df header (server reads a header-less section as yellow)"

pass "xymonclient-openbsd.sh FS filter: types, local-only, inode '-' drop, df-failure marker"
