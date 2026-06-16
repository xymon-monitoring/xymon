#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/client/fs-filter-netbsd.sh
#
# Regression guard for the df filesystem filter ported to
# xymonclient-netbsd.sh (issue #170). NetBSD's client has no [inode] section;
# this covers the [df] type filter (XYMONCLIENT_FS_{INCLUDE,EXCLUDE}_TYPES via
# df "-t no<csv>") and df -l (XYMONCLIENT_FS_DF_LOCAL_ONLY).
#
# Mock-tested on any host (df-argv construction). Real NetBSD df still needs
# verifying on NetBSD.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
# shellcheck source=tests/client/fs-filter-common.sh
. "$(dirname "$0")/fs-filter-common.sh"

# Resolve SCRIPT, apply the dangling-override / skip-if-absent contract, assert
# the filter is present, set up TMP/STUB/DF_LOG/INODE_LOG/STDERR_LOG/PATH.
# (INODE_LOG is unused here: NetBSD's client has no [inode] section.)
fsf_setup netbsd XYMONCLIENT_NETBSD

cat > "$STUB/df" <<EOF
#!/usr/bin/env bash
# DF_FAIL=1 simulates a df that exits non-zero with no output (e.g. killed).
[ -n "\${DF_FAIL:-}" ] && exit 1
echo "\$*" >> "$DF_LOG"
printf 'Filesystem 1024-blocks Used Available Capacity Mounted on\n'
printf '/dev/wd0a 100 50 50 50%% /\n'
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
assert_contains " -P " "$args" "disk df keeps -P"
assert_contains " -l " "$args" "default is local-only (df -l)"
assert_contains " -tnokernfs,procfs,cd9660,null " "$args" \
	"default excludes pseudo types via -t no<csv>"
assert_not_contains "nonfs" "$args" "nfs is hidden by df -l, not by the type list"

# --- INCLUDE / EXCLUDE_TYPES ------------------------------------------------
args=$(XYMONCLIENT_FS_INCLUDE_TYPES=kernfs run)
assert_not_contains "kernfs" "$args" "INCLUDE_TYPES=kernfs un-excludes kernfs"
assert_contains "procfs" "$args" "INCLUDE_TYPES=kernfs leaves the others"

args=$(XYMONCLIENT_FS_EXCLUDE_TYPES=ext2fs run)
assert_contains "ext2fs" "$args" "EXCLUDE_TYPES=ext2fs adds ext2fs"

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

# --- DF_LOCAL_ONLY ----------------------------------------------------------
args=$(XYMONCLIENT_FS_DF_LOCAL_ONLY=no run)
assert_not_contains " -l " "$args" "DF_LOCAL_ONLY=no drops -l"
args=$(XYMONCLIENT_FS_DF_LOCAL_ONLY=bogus run)
assert_contains " -l " "$args" "invalid DF_LOCAL_ONLY falls back to local-only"
assert_contains "invalid XYMONCLIENT_FS_DF_LOCAL_ONLY" "$(cat "$STDERR_LOG")" \
	"invalid DF_LOCAL_ONLY warns"

# --- false-green guard: df fails with no output -> marker, not empty section --
out=$( cd "$TMP"; DF_FAIL=1 /bin/sh "$SNIPPET" 2>/dev/null )
assert_contains "Disk report collection failed" "$out" \
	"a failed df emits a failure marker"
assert_not_contains "Filesystem" "$out" \
	"failure marker carries no df header (server reads a header-less section as yellow)"

pass "xymonclient-netbsd.sh FS filter: types, local-only, df-failure marker"
