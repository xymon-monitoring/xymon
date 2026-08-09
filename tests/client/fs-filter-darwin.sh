#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/client/fs-filter-darwin.sh
#
# Regression guard for the df/inode filesystem filter ported to
# xymonclient-darwin.sh (issue #170). macOS df selects per path (no -t), so the
# filter is applied when building the filesystem list from mount(8):
# XYMONCLIENT_FS_EXCLUDE_TYPES drops extra attributes/types, INCLUDE_TYPES keeps
# defaults, and XYMONCLIENT_FS_DF_LOCAL_ONLY keeps only MNT_LOCAL filesystems
# (mount(8) marks them "local"), mirroring df -l on the Linux/*BSD clients.
# Modern-macOS semantics (fixture measured on a real Apple-silicon Mac):
# the "root data"-flagged data volume survives its nobrowse flag, the sealed
# read-only root is dropped, apfs never reaches the [inode] report (container-
# derived ifree carries no signal), an all-apfs inode list is silent-empty,
# and an empty DISK list emits the yellow marker instead of a silent blank.
#
# Mock-tested on any host (mount-filtering + df-argv). The snippet uses IFS=$'\n',
# which the macOS /bin/sh (bash) supports but dash does not, so we run it with
# bash here, matching macOS. Real macOS df/mount still need verifying on macOS.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
# shellcheck source=tests/client/fs-filter-common.sh
. "$(dirname "$0")/fs-filter-common.sh"

# Resolve SCRIPT, apply the dangling-override / skip-if-absent contract, assert
# the filter is present, set up TMP/STUB/DF_LOG/INODE_LOG/STDERR_LOG/PATH.
fsf_setup darwin XYMONCLIENT_DARWIN
command -v column >/dev/null 2>&1 || skip "column(1) not available"

# mount stub: macOS-style lines. Local filesystems carry the "local" attribute
# (MNT_LOCAL); remote ones (afs, nfs) do not. Covers each default-dropped
# attribute (nobrowse, read-only) and both a local and a remote read-only/remote
# mount so LOCAL_ONLY and the attribute filter can be told apart.
# Mirrors a real Apple-silicon Mac mini (measured 2026-07): sealed read-only
# root, every system volume nobrowse, the data volume marked "root data" -
# plus test extras: an external apfs disk, two hfs USB disks (the inode
# report's non-apfs population), a local read-only image, and remote mounts.
cat > "$STUB/mount" <<'EOF'
#!/usr/bin/env bash
cat <<'MOUNT'
/dev/disk3s1s1 on / (apfs, sealed, local, read-only, journaled)
devfs on /dev (devfs, local, nobrowse)
/dev/disk3s6 on /System/Volumes/VM (apfs, local, noexec, journaled, noatime, nobrowse)
/dev/disk3s5 on /System/Volumes/Data (apfs, local, journaled, nobrowse, protect, root data)
map auto_home on /System/Volumes/Data/home (autofs, automounted, nobrowse)
/dev/disk5s1 on /Volumes/External (apfs, local, nodev, nosuid, journaled)
/dev/disk6s1 on /Volumes/USBHFS (hfs, local, nodev, nosuid, journaled)
/dev/disk7s1 on /Volumes/USBHFS2 (hfs, local, nodev, nosuid, journaled)
/dev/disk2 on /Volumes/CD (cd9660, local, read-only, nobrowse)
remote:/exp on /Volumes/nfs (nfs, nodev, nosuid)
MOUNT
EOF
chmod +x "$STUB/mount"
# df stub: record argv (last arg is the mountpoint). Inode (-i) calls are routed
# to a separate log and emit the wider macOS "df -i" table; disk calls emit the
# plain table. This lets the test prove the [inode] block queries every
# filesystem in inode mode, not disk mode.
cat > "$STUB/df" <<EOF
#!/usr/bin/env bash
# DF_FAIL=1 simulates a df that exits non-zero with no output (e.g. killed).
[ -n "\${DF_FAIL:-}" ] && exit 1
for _m; do :; done   # _m = last arg = the mountpoint df was asked about
if [[ " \$* " =~ " -i " ]]; then
	echo "\$*" >> "$INODE_LOG"
	printf 'Filesystem 1024-blocks Used Available Capacity iused ifree %%iused Mounted on\n'
	printf '/dev/disk 100 50 50 50%% 1000 9000 10%% %s\n' "\$_m"
else
	echo "\$*" >> "$DF_LOG"
	printf 'Filesystem 1024-blocks Used Available Capacity Mounted on\n'
	printf '/dev/disk 100 50 50 50%% %s\n' "\$_m"
fi
EOF
chmod +x "$STUB/df"
# Decoy files in the snippet's working directory ($TMP): if the script let a
# configured type token glob, "read-onl*"/"hf*" would expand to these filenames.
: > "$TMP/read-only"
: > "$TMP/hfs"

SNIPPET="$TMP/df-section.sh"
fsf_extract "$SNIPPET"

# Not fsf_run: the snippet uses IFS=$'\n', which macOS /bin/sh (bash) supports
# but dash does not, so it must run under bash here, matching macOS.
run() {
	: > "$DF_LOG"; : > "$INODE_LOG"; : > "$STDERR_LOG"
	( cd "$TMP"; bash "$SNIPPET" >/dev/null 2>"$STDERR_LOG" )
	printf ' %s ' "$(tr '\n' ' ' < "$DF_LOG")"
}

# --- default: modern-macOS semantics -----------------------------------------
# The data volume survives its nobrowse flag via the "root data" exemption;
# the sealed read-only root cannot fill and is dropped; system helpers,
# devfs, remote and read-only mounts are dropped.
args=$(run)
assert_contains "/System/Volumes/Data" "$args" "root-data exemption keeps the data volume despite nobrowse"
assert_contains "/Volumes/External" "$args"    "default keeps a plain local fs"
assert_contains "/Volumes/USBHFS " "$args"     "default keeps a second local fs"
assert_not_contains " -H / " "$args"           "default drops the sealed read-only root (cannot fill)"
assert_not_contains "/System/Volumes/VM" "$args" "default drops a nobrowse system helper"
assert_not_contains "/dev " "$args"            "default drops devfs (nobrowse)"
assert_not_contains "/Volumes/nfs" "$args"     "default (local-only) drops the nfs (remote) mount"
assert_not_contains "home" "$args"             "default drops the autofs map (nobrowse)"
assert_not_contains "/Volumes/CD" "$args"      "default drops a read-only mount"

# --- EXCLUDE_TYPES drops an extra type --------------------------------------
args=$(XYMONCLIENT_FS_EXCLUDE_TYPES=hfs run)
assert_not_contains "/Volumes/USBHFS" "$args" "EXCLUDE_TYPES=hfs drops the hfs mounts"
assert_contains "/Volumes/External" "$args"   "EXCLUDE_TYPES=hfs leaves other types alone"

# An explicit exclude beats the root-data exemption: the admin said apfs goes.
args=$(XYMONCLIENT_FS_EXCLUDE_TYPES=apfs run)
assert_not_contains "/System/Volumes/Data" "$args" "EXCLUDE_TYPES=apfs beats the root-data exemption"
assert_not_contains "/Volumes/External" "$args"    "EXCLUDE_TYPES=apfs drops every apfs mount"
assert_contains "/Volumes/USBHFS " "$args"         "EXCLUDE_TYPES=apfs leaves hfs alone"

# --- INCLUDE_TYPES un-excludes a default-dropped attribute ------------------
# read-only is an always-drop attribute; including it surfaces the sealed
# root (but not the CD image, which is also nobrowse). nobrowse surfaces the
# system helper volumes.
args=$(XYMONCLIENT_FS_INCLUDE_TYPES=read-only run)
assert_contains " -H / " "$args" "INCLUDE_TYPES=read-only surfaces the sealed root"
assert_not_contains "/Volumes/CD" "$args" "INCLUDE_TYPES=read-only still drops the nobrowse read-only mount"
args=$(XYMONCLIENT_FS_INCLUDE_TYPES=nobrowse run)
assert_contains "/System/Volumes/VM" "$args" "INCLUDE_TYPES=nobrowse surfaces the system helper volumes"

# --- XYMONCLIENT_FS_DF_LOCAL_ONLY -------------------------------------------
# "no" surfaces remote filesystems (parity with df -l elsewhere).
args=$(XYMONCLIENT_FS_DF_LOCAL_ONLY=no run)
assert_contains "/Volumes/nfs" "$args" "DF_LOCAL_ONLY=no surfaces the nfs (remote) mount"
assert_contains "/System/Volumes/Data" "$args" "DF_LOCAL_ONLY=no still keeps local filesystems"
# An invalid value warns and falls back to local-only (remote stays hidden).
args=$(XYMONCLIENT_FS_DF_LOCAL_ONLY=bogus run)
assert_not_contains "/net/server" "$args" "invalid DF_LOCAL_ONLY falls back to local-only"
assert_contains "invalid XYMONCLIENT_FS_DF_LOCAL_ONLY" "$(cat "$STDERR_LOG")" "invalid DF_LOCAL_ONLY warns"

# --- type tokens are literal, not shell globs or regexes --------------------
# With $TMP as the working directory (decoy files read-only/hfs present), an
# include glob must not expand to "read-only" and surface the sealed root.
args=$(XYMONCLIENT_FS_INCLUDE_TYPES='read-onl*' run)
assert_not_contains " -H / " "$args" \
	"include type tokens must not undergo pathname expansion"
# An exclude glob must not expand to "hfs" and drop the hfs mounts.
args=$(XYMONCLIENT_FS_EXCLUDE_TYPES='hf*' run)
assert_contains "/Volumes/USBHFS " "$args" \
	"exclude type tokens must not undergo pathname expansion"
# An exclude token must be matched literally in the mount filter, not as an ERE:
# "hf." must not match "hfs" via the regex "." metacharacter.
args=$(XYMONCLIENT_FS_EXCLUDE_TYPES='hf.' run)
assert_contains "/Volumes/USBHFS " "$args" \
	"exclude type tokens must be matched literally, not as a regex"

# --- include + exclude precedence: exclude wins -----------------------------
# A type named in both lists stays excluded: EXCLUDE is applied last (documented
# contract). read-only in both must keep the local read-only mount dropped.
args=$(XYMONCLIENT_FS_INCLUDE_TYPES=read-only XYMONCLIENT_FS_EXCLUDE_TYPES=read-only run)
assert_not_contains " -H / " "$args" \
	"a type in both include and exclude lists must stay excluded (exclude wins)"

# --- [inode]: apfs excluded, every remaining fs queried in inode mode -------
# apfs ifree is container-derived (identical across volumes, %iused pinned at
# 0% - measured on a real Mac), so apfs rows never reach the inode report;
# the two hfs USB disks do. Regression guard retained: the loop body once used
# `df -P -H` for the 2nd+ filesystem, mislabelling disk data as inode data.
args=$(run)
inode_args=$(printf ' %s ' "$(tr '\n' ' ' < "$INODE_LOG")")
assert_contains " -i " "$inode_args" "inode report uses df -i"
assert_not_contains " -H " "$inode_args" "inode report must NOT use df -H (would mislabel disk as inode)"
assert_not_contains "Data" "$inode_args" "apfs volumes are excluded from the inode report"
assert_equal 2 "$(grep -c . "$INODE_LOG")" "inode report queries every kept filesystem, not just the first"

out=$( cd "$TMP"; bash "$SNIPPET" 2>/dev/null )
inode_section=$(printf '%s\n' "$out" | sed -n '/^\[inode\]/,$p')
assert_contains "/Volumes/USBHFS2" "$inode_section" "inode report includes the 2nd filesystem's row"

# All-APFS Mac: the inode list is legitimately empty - the section must be
# empty and silent (nothing inode-limited exists), NOT a failure marker.
: > "$INODE_LOG"
out=$( cd "$TMP"; XYMONCLIENT_FS_EXCLUDE_TYPES=hfs bash "$SNIPPET" 2>/dev/null )
inode_section=$(printf '%s\n' "$out" | sed -n '/^\[inode\]/,$p')
assert_not_contains "collection failed" "$inode_section" \
	"an all-apfs inode list is empty data, not a failure"
assert_equal "" "$(cat "$INODE_LOG")" "no inode df runs on an all-apfs system"
assert_contains "/System/Volumes/Data" "$out" "the disk report still carries the data volume"

# --- empty disk list: no path-less df, and a LOUD marker ---------------------
# If every mount is excluded, df must not run with no path (which would list
# all mounts and defeat the filter) - and the disk report must go yellow, not
# silently green: on a modern Mac a wrong default here blanks all monitoring.
: > "$DF_LOG"; : > "$INODE_LOG"
out=$( cd "$TMP"; XYMONCLIENT_FS_EXCLUDE_TYPES="apfs hfs" bash "$SNIPPET" 2>/dev/null )
assert_equal "" "$(cat "$DF_LOG")"    "all-excluded: disk df not run with an empty path"
assert_equal "" "$(cat "$INODE_LOG")" "all-excluded: inode df not run with an empty path"
assert_contains "Disk report collection failed" "$out" \
	"an empty disk list emits the failure marker (yellow), never a silent blank"

# --- false-green guard: df fails with no output -> marker, not empty section --
# Mirrors the *BSD clients: a df that produces nothing must emit a failure
# marker (no df header) so the server goes yellow, not green.
out=$( cd "$TMP"; DF_FAIL=1 bash "$SNIPPET" 2>/dev/null )
assert_contains "Disk report collection failed" "$out" \
	"a failed disk df emits a failure marker"
assert_contains "Inode report collection failed" "$out" \
	"a failed inode df emits a failure marker"
assert_not_contains "Filesystem" "$out" \
	"failure marker carries no df header (server reads a header-less section as yellow)"

pass "xymonclient-darwin.sh FS filter: root-data exemption, apfs-free inode report, empty-list marker"
