#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/client/fs-filter-freebsd.sh
#
# Regression guard for the df/inode filesystem filter ported to
# xymonclient-freebsd.sh (issue #170). FreeBSD df excludes by type with
# "-t no<csv>"; the script builds that list from XYMONCLIENT_FS_{INCLUDE,
# EXCLUDE}_TYPES, hides remote fs with df -l (XYMONCLIENT_FS_DF_LOCAL_ONLY),
# drops no-inode-limit rows ("-") from [inode], and excludes zfs and tmpfs
# from [inode] by type (zfs has no inode limit; FreeBSD tmpfs reports the
# 2^31-1 sentinel as itotal, so the "-" row guard cannot catch it).
#
# Mock-tested on any host: extract the [df]..[mount] block, run it with a df
# stub that records argv, and assert the constructed options. (Real FreeBSD df
# output semantics still need verifying on FreeBSD.)

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
# shellcheck source=tests/client/fs-filter-common.sh
. "$(dirname "$0")/fs-filter-common.sh"

# Resolve SCRIPT (in-tree default; $XYMONCLIENT_FREEBSD points at the installed
# copy), apply the dangling-override / skip-if-absent contract, assert the FS
# filter is still present (its absence is a regression, not a skip), and set up
# TMP/STUB/DF_LOG/INODE_LOG/STDERR_LOG/PATH.
fsf_setup freebsd XYMONCLIENT_FREEBSD

# df stub: record argv (inode -i calls routed separately) and emit a table. The
# inode table includes a "tank" row with "-" %iused (a no-inode-limit fs).
cat > "$STUB/df" <<EOF
#!/usr/bin/env bash
# DF_FAIL=1 simulates a df that exits non-zero with no output (e.g. killed).
[ -n "\${DF_FAIL:-}" ] && exit 1
if [[ " \$* " =~ " -i " ]]; then
	echo "\$*" >> "$INODE_LOG"
	printf 'Filesystem Size Used Avail Capacity iused ifree %%iused Mounted on\n'
	printf '/dev/ada0p2 100 50 50 50%% 1000 9000 10%% /\n'
	printf 'tank 200 1 199 1%% 0 0 - /tank\n'
	printf 'zdata/poudriere 200 1 199 1%% 5 95 5%% /poudriere/data\n'
else
	echo "\$*" >> "$DF_LOG"
	printf 'Filesystem Size Used Avail Capacity Mounted on\n'
	printf '/dev/ada0p2 100G 50G 50G 50%% /\n'
	printf 'zdata/photos 200G 10G 190G 5%% /zdata/photos\n'
	printf 'zdata/poudriere 200G 10G 190G 5%% /poudriere/data\n'
	printf 'zdata/poudriere2 200G 10G 190G 5%% /poudriere/jails\n'
fi
EOF
chmod +x "$STUB/df"

# Decoy files in the snippet's working directory ($TMP): if the script let a
# configured type token glob, "procf*"/"fuse.*" would expand to these filenames.
: > "$TMP/procfs"
: > "$TMP/fuse.sshfs"

# Combined [df]+[inode] block (default stop at [mount]); the [inode] block reuses
# the exclude list the [df] block built, so they run together. run() is a thin
# shim over fsf_run (fs-filter-common.sh) returning the disk df argv, padded.
SNIPPET="$TMP/df-section.sh"
fsf_extract "$SNIPPET"
run() { fsf_run "$SNIPPET" "$DF_LOG"; }

# --- default behaviour ------------------------------------------------------
args=$(run)
assert_contains " -H " "$args" "disk df keeps -H"
assert_contains " -l " "$args" "default is local-only (df -l)"
assert_contains " -tnonullfs,cd9660,procfs,devfs,linprocfs,fdescfs,autofs " "$args" \
	"default excludes pseudo types via -t no<csv>"
assert_not_contains "nonfs" "$args" "nfs is hidden by df -l, not by the type list"

inode_args=$(printf ' %s ' "$(tr '\n' ' ' < "$INODE_LOG")")
assert_contains " -i " "$inode_args" "inode df uses -i"
assert_contains "zfs" "$inode_args" "inode report excludes zfs by default"
assert_contains "tmpfs" "$inode_args" \
	"inode report excludes tmpfs (itotal is the 2^31-1 sentinel, not a limit)"
assert_not_contains "tmpfs" "$args" \
	"the disk report keeps tmpfs (RAM-backed capacity is real)"

# The per-report excludes go through the INCLUDE filter, so an admin who wants
# the tmpfs inode rows back has the documented escape hatch.
args=$(XYMONCLIENT_FS_INCLUDE_TYPES=tmpfs run)
inode_args=$(printf ' %s ' "$(tr '\n' ' ' < "$INODE_LOG")")
assert_not_contains "tmpfs" "$inode_args" \
	"INCLUDE_TYPES=tmpfs un-excludes tmpfs from the inode report"

# --- INCLUDE / EXCLUDE_TYPES ------------------------------------------------
args=$(XYMONCLIENT_FS_INCLUDE_TYPES=nullfs run)
assert_not_contains "nullfs" "$args" "INCLUDE_TYPES=nullfs un-excludes nullfs"
assert_contains "cd9660" "$args" "INCLUDE_TYPES=nullfs leaves the other excludes"

args=$(XYMONCLIENT_FS_EXCLUDE_TYPES=ext2fs run)
assert_contains "ext2fs" "$args" "EXCLUDE_TYPES=ext2fs adds ext2fs to the list"
assert_contains "cd9660" "$args" "EXCLUDE_TYPES leaves the defaults"

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
# contract, "If a type appears in both lists, EXCLUDE wins"). ext2fs is not a
# default, so its presence is unambiguous.
args=$(XYMONCLIENT_FS_INCLUDE_TYPES=ext2fs XYMONCLIENT_FS_EXCLUDE_TYPES=ext2fs run)
assert_contains "ext2fs" "$args" \
	"a type in both include and exclude lists must stay excluded (exclude wins)"

# --- DF_LOCAL_ONLY ----------------------------------------------------------
args=$(XYMONCLIENT_FS_DF_LOCAL_ONLY=no run)
assert_not_contains " -l " "$args" "DF_LOCAL_ONLY=no drops -l (surfaces remote)"
args=$(XYMONCLIENT_FS_DF_LOCAL_ONLY=bogus run)
assert_contains " -l " "$args" "invalid DF_LOCAL_ONLY falls back to local-only"
assert_contains "invalid XYMONCLIENT_FS_DF_LOCAL_ONLY" "$(cat "$STDERR_LOG")" \
	"invalid DF_LOCAL_ONLY warns"

# --- inode "-" (no inode limit) rows are dropped ----------------------------
out=$( cd "$TMP"; /bin/sh "$SNIPPET" 2>/dev/null )
inode_section=$(printf '%s\n' "$out" | sed -n '/^\[inode\]/,$p')
assert_not_contains "/tank" "$inode_section" \
	"inode report drops the no-inode-limit filesystem (%iused '-')"

# --- XYMONCLIENT_FS_EXCLUDE_MOUNTS: drop mounts by glob pattern --------------
# Output-level cases (the type filters assert on df argv; this filter runs on
# df's OUTPUT): the stub emits /, /zdata/photos, /poudriere/data and
# /poudriere/jails in [df], and /, /tank, /poudriere/data in [inode].
out=$( cd "$TMP"; /bin/sh "$SNIPPET" 2>/dev/null )
assert_contains "/poudriere/data" "$out" "default: no mounts are excluded"

out=$( cd "$TMP"; XYMONCLIENT_FS_EXCLUDE_MOUNTS='/poudriere/*' /bin/sh "$SNIPPET" 2>/dev/null )
disk_section=$(printf '%s\n' "$out" | sed -n '1,/^\[inode\]/p')
inode_section=$(printf '%s\n' "$out" | sed -n '/^\[inode\]/,$p')
assert_contains     "Mounted on"        "$disk_section" "header row survives the mount filter"
assert_contains     "/zdata/photos"     "$disk_section" "non-matching mounts are kept"
assert_not_contains "/poudriere/data"   "$disk_section" "glob excludes a matching mount from [df]"
assert_not_contains "/poudriere/jails"  "$disk_section" "glob excludes every matching mount"
assert_not_contains "/poudriere/data"   "$inode_section" "glob excludes the mount from [inode] too"
assert_contains     "/dev/ada0p2"       "$inode_section" "non-matching inode rows are kept"

# A bare '*' must act as a match-everything pattern (noglob: it must not
# expand against the decoy files in the working directory) and keep headers.
out=$( cd "$TMP"; XYMONCLIENT_FS_EXCLUDE_MOUNTS='*' /bin/sh "$SNIPPET" 2>/dev/null )
assert_contains     "Mounted on"  "$out" "header rows survive even a '*' pattern"
assert_not_contains "/dev/ada0p2" "$out" "'*' drops every mount row"

# --- false-green guard: df fails with no output -> marker, not empty section --
out=$( cd "$TMP"; DF_FAIL=1 /bin/sh "$SNIPPET" 2>/dev/null )
assert_contains "Disk report collection failed" "$out" \
	"a failed disk df emits a failure marker"
assert_contains "Inode report collection failed" "$out" \
	"a failed inode df emits a failure marker"
assert_not_contains "Filesystem" "$out" \
	"failure marker carries no df header (server reads a header-less section as yellow)"

pass "xymonclient-freebsd.sh FS filter: types, mounts, local-only, inode zfs/tmpfs+'-' drop, df-failure marker"
