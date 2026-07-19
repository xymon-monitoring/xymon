#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/client/fs-filter-linux.sh
#
# Regression guard for xymon-monitoring/xymon#96 (refs #49):
# xymonclient-linux.sh respects four environment variables that tune
# which filesystems appear in the df/inode blocks of the client report:
#
#   XYMONCLIENT_FS_INCLUDE_TYPES   types to un-exclude (e.g. tmpfs)
#   XYMONCLIENT_FS_EXCLUDE_TYPES   additional types to exclude
#   XYMONCLIENT_FS_DF_LOCAL_ONLY   yes (default, df -l) vs no
#
# The script reads /proc/filesystems directly and shells out to df.
# To test in isolation we extract the [df] section from the script,
# rewrite the /proc/filesystems path to point at a fixture, and run it
# with a df stub that records the command-line it was invoked with.
#
# The extraction (sed pattern below) is the brittle bit -- if anyone
# restructures xymonclient-linux.sh in a way that breaks the
# `echo "[df]"` ... `echo "[inode]"` block boundary, this test will
# stop catching the actual filter logic. That trade is conscious:
# wrapping every shell-out in the full script would need stubs for
# uptime, who, vmstat, top, free, ifconfig, iostat, ps and several
# others, which is more surface than the test buys back.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
# shellcheck source=tests/client/fs-filter-common.sh
. "$(dirname "$0")/fs-filter-common.sh"

# Resolve SCRIPT (in-tree default; $XYMONCLIENT_LINUX, set by autopkgtest, points
# at the INSTALLED script so we test what the package ships -- same-version
# artifacts only, see "Path discovery" in tests/README.md), apply the
# fail-on-dangling-override / skip-if-absent contract, assert the #49/#96 FS
# filter is still present (its absence is a regression, not a skip, per
# tests/README.md), and set up TMP/STUB/DF_LOG/INODE_LOG/STDERR_LOG/PATH.
fsf_setup linux XYMONCLIENT_LINUX

# --- fixtures ----------------------------------------------------------------

# Representative /proc/filesystems. Genuine pseudo-FS that must default-exclude
# (sysfs, proc, overlay, nfs, nfs4); real local FS that merely happen to be nodev
# and so are default-INCLUDED (tmpfs, zfs); the never-excluded special case
# (rootfs); and two device-backed FS that must not appear in the nodev list.
cat > "$TMP/proc.filesystems" <<'EOF'
nodev	sysfs
nodev	tmpfs
nodev	zfs
nodev	proc
	ext4
	xfs
nodev	overlay
nodev	nfs
nodev	nfs4
nodev	rootfs
EOF

# Matching filenames expose accidental pathname expansion of configured
# filesystem type tokens when snippets run with $TMP as their working directory.
: > "$TMP/tmpfs"
: > "$TMP/fuse.sshfs"

# Extract just the [df] block (stop at [inode]) and the combined [df]+[inode]
# block (stop at [mount]), rewriting the /proc/filesystems reference at the
# fixture in both. The [inode] block reuses the EXCLUDES/ROOTFS the [df] block
# computed, so the combined block must run them together -- a lone [inode]
# snippet would see an empty EXCLUDES.
PROC_REWRITE="s!/proc/filesystems!$TMP/proc.filesystems!g"
SNIPPET="$TMP/df-section.sh"
fsf_extract "$SNIPPET" "$PROC_REWRITE" '\[inode\]'
COMBINED="$TMP/df-inode-section.sh"
fsf_extract "$COMBINED" "$PROC_REWRITE"

# --- stubs -------------------------------------------------------------------

# df stub: append its full argv to a log file, emit a minimal valid table
# so the downstream `sed` in the script has something to chew on. The [df] and
# [inode] blocks both shell out to df; route the inode invocation (`df -Pil` /
# `df -i`) to its own log so each block's flags can be asserted independently.
cat > "$STUB/df" <<EOF
#!/usr/bin/env bash
if [[ " \$* " =~ -P[a-zA-Z]*i ]] || [[ " \$* " =~ " -i " ]]; then
	echo "\$*" >> "$INODE_LOG"
else
	echo "\$*" >> "$DF_LOG"
fi
printf 'Filesystem 1024-blocks Used Available Capacity Mounted on\n'
printf '/dev/sda1 1000000 500000 500000 50%% /\n'
EOF
chmod +x "$STUB/df"

# readlink stub: the script does `readlink -m /dev/root` early in the
# block. Print a sentinel so the sed substitution in the script succeeds.
cat > "$STUB/readlink" <<'EOF'
#!/usr/bin/env bash
echo "/dev/sda1"
EOF
chmod +x "$STUB/readlink"

# --- runner ------------------------------------------------------------------

# run_snippet runs the [df]-only block and returns the df argv; run_inode runs
# the combined block and returns the *inode* invocation's argv (the df stub
# routes it to INODE_LOG). Both are thin shims over fsf_run (fs-filter-common.sh)
# so the call sites and the inline per-case env prefix stay unchanged; see the
# leak warning below.
run_snippet() { fsf_run "$SNIPPET" "$DF_LOG"; }
run_inode()   { fsf_run "$COMBINED" "$INODE_LOG"; }

# Callers pass per-case env vars via the inline prefix on the
# `args=$(VAR=val run_snippet)` invocation. Do NOT also set them on
# the outer `args=$(...)` -- e.g. `VAR=val args=$(VAR=val run_snippet)`
# is a *single* simple command with no command word, which makes both
# assignments target the calling shell. The outer one then leaks VAR
# into every subsequent case. Use a single inline prefix on the
# command-substitution side only.

# --- default behaviour: -P -l, every nodev (except rootfs) excluded ---------

args=$(run_snippet)

assert_contains " -P "        "$args" "default must pass -P to df"
assert_contains " -l "        "$args" "default must pass -l to df (local only)"
assert_contains " -x iso9660 " "$args" "default must exclude iso9660 (always-full image)"
assert_contains " -x squashfs " "$args" "default must exclude squashfs (always-full image)"
assert_contains " -x fuse.snapfuse " "$args" "default must exclude fuse.snapfuse (snap FUSE image, always 100%)"
assert_contains " -x sysfs "  "$args" "default must exclude sysfs (pseudo nodev)"
assert_contains " -x overlay " "$args" "default must exclude overlay (pseudo nodev)"
assert_contains " -x nfs "    "$args" "default must exclude nfs (pseudo nodev)"
assert_not_contains " -x tmpfs " "$args" "default must INCLUDE tmpfs (real local fs that is nodev)"
assert_not_contains " -x zfs "   "$args" "default must INCLUDE zfs (real local fs that is nodev)"
assert_not_contains " -x rootfs " "$args" "default must NOT exclude rootfs (special case)"
assert_not_contains " -x ext4 " "$args" "default must NOT exclude ext4 (device-backed)"

# --- XYMONCLIENT_FS_INCLUDE_TYPES un-excludes one type ----------------------

args=$(XYMONCLIENT_FS_INCLUDE_TYPES=tmpfs run_snippet)
assert_not_contains " -x tmpfs " "$args" \
	"XYMONCLIENT_FS_INCLUDE_TYPES=tmpfs must drop tmpfs from -x list"
assert_contains " -x sysfs " "$args" \
	"XYMONCLIENT_FS_INCLUDE_TYPES=tmpfs must not affect other excludes"

# Multiple include types in one call.
args=$(XYMONCLIENT_FS_INCLUDE_TYPES="tmpfs overlay" run_snippet)
assert_not_contains " -x tmpfs "   "$args" "multi-include must drop tmpfs"
assert_not_contains " -x overlay " "$args" "multi-include must drop overlay"
assert_contains     " -x sysfs "   "$args" "multi-include must leave sysfs"

# Type tokens are literal, not shell globs. The matching $TMP/tmpfs file must
# not turn "tmp*" into "tmpfs" and accidentally un-exclude tmpfs.
args=$(XYMONCLIENT_FS_INCLUDE_TYPES='tmp*' run_snippet)
assert_contains " -x tmpfs " "$args" \
	"include type tokens must not undergo pathname expansion"

# --- XYMONCLIENT_FS_EXCLUDE_TYPES adds extra exclusions ---------------------

args=$(XYMONCLIENT_FS_EXCLUDE_TYPES=ext4 run_snippet)
assert_contains " -x ext4 "  "$args" "XYMONCLIENT_FS_EXCLUDE_TYPES=ext4 must add ext4"
assert_contains " -x sysfs " "$args" "extra exclude must not displace defaults"

# Extra exclude that's already in the default list must not be duplicated.
# `grep -o | wc -l` counts occurrences across one line (grep -c would cap at
# 1 per line, hiding a duplicate); BSD/macOS wc pads its output with leading
# spaces, so normalize through arithmetic before comparing.
args=$(XYMONCLIENT_FS_EXCLUDE_TYPES=tmpfs run_snippet)
tmpfs_count=$(printf '%s' "$args" | grep -o -- '-x tmpfs' | wc -l)
tmpfs_count=$((tmpfs_count))
assert_equal 1 "$tmpfs_count" \
	"XYMONCLIENT_FS_EXCLUDE_TYPES=tmpfs must not duplicate an existing exclude"

# The matching $TMP/fuse.sshfs file must not rewrite the configured literal.
args=$(XYMONCLIENT_FS_EXCLUDE_TYPES='fuse.*' run_snippet)
assert_contains " -x fuse.* " "$args" \
	"exclude type tokens must not undergo pathname expansion"
assert_not_contains " -x fuse.sshfs " "$args" \
	"exclude type glob must not expand to a working-directory filename"

# --- include + exclude precedence: exclude wins -----------------------------
#
# INCLUDE is applied first (un-excludes the type), then EXCLUDE re-adds it,
# so a type named in both lists stays excluded -- the documented contract
# ("If a type appears in both lists, EXCLUDE wins").
args=$(XYMONCLIENT_FS_INCLUDE_TYPES=tmpfs XYMONCLIENT_FS_EXCLUDE_TYPES=tmpfs run_snippet)
assert_contains " -x tmpfs " "$args" \
	"a type in both include and exclude lists must stay excluded (exclude wins)"

# --- XYMONCLIENT_FS_DF_LOCAL_ONLY=no drops the -l flag ----------------------

args=$(XYMONCLIENT_FS_DF_LOCAL_ONLY=no run_snippet)
assert_contains     " -P " "$args" "DF_LOCAL_ONLY=no must still pass -P"
assert_not_contains " -l " "$args" "DF_LOCAL_ONLY=no must drop -l"

args=$(XYMONCLIENT_FS_DF_LOCAL_ONLY=yes run_snippet)
assert_contains " -l " "$args" "DF_LOCAL_ONLY=yes must still pass -l"

# An invalid value must fail safe rather than silently exposing remote mounts.
args=$(XYMONCLIENT_FS_DF_LOCAL_ONLY=YES run_snippet)
assert_contains " -l " "$args" \
	"invalid DF_LOCAL_ONLY value must fall back to local-only mode"
assert_contains "invalid XYMONCLIENT_FS_DF_LOCAL_ONLY" "$(cat "$STDERR_LOG")" \
	"invalid DF_LOCAL_ONLY value must produce a warning"

# --- [inode] report must be filtered the same way as [df] -------------------
#
# The [inode] block runs `df -Pil -x $EXCLUDES`, reusing the exact exclude set
# the [df] block built (iso9660 is part of that set via the EXCLUDE_TYPES
# default, no longer a separate hardcoded -x). Extracting [df] alone (the SNIPPET above)
# never exercises this second df invocation, so a regression that leaves the
# inode report unfiltered would go unnoticed. run_inode runs both blocks and
# returns the *inode* invocation's argv; assert the same nodev/rootfs/real-FS
# rules hold there.

iargs=$(run_inode)
assert_contains     " -x sysfs "  "$iargs" "inode report must exclude sysfs (pseudo nodev), same as df"
assert_not_contains " -x tmpfs "  "$iargs" "inode report must INCLUDE tmpfs (real local fs), same as df"
assert_contains     " -x overlay " "$iargs" "inode report must exclude overlay (pseudo nodev), same as df"
assert_not_contains " -x ext4 "   "$iargs" "inode report must NOT exclude ext4 (non-nodev)"
assert_not_contains " -x rootfs " "$iargs" "inode report must NOT exclude rootfs (special case)"

# The include/exclude env vars must reach the inode block too, not just [df].
iargs=$(XYMONCLIENT_FS_INCLUDE_TYPES=tmpfs run_inode)
assert_not_contains " -x tmpfs " "$iargs" \
	"XYMONCLIENT_FS_INCLUDE_TYPES=tmpfs must also drop tmpfs from the inode report"
iargs=$(XYMONCLIENT_FS_EXCLUDE_TYPES=ext4 run_inode)
assert_contains " -x ext4 " "$iargs" \
	"XYMONCLIENT_FS_EXCLUDE_TYPES=ext4 must also add ext4 to the inode report"

# --- /proc/filesystems missing: warn, no excludes (script must not crash) ---

# Repoint the snippet at a non-existent path; the [-r ...] guard should
# kick in, the warning go to stderr, and df be invoked without any nodev
# -x flags (iso9660 is always passed).
MISSING_SNIPPET="$TMP/df-section-missing.sh"
sed "s!$TMP/proc.filesystems!$TMP/does-not-exist!g" "$SNIPPET" > "$MISSING_SNIPPET"

: > "$DF_LOG"
/bin/sh "$MISSING_SNIPPET" >/dev/null 2>"$TMP/stderr"
args=$(printf ' %s ' "$(cat "$DF_LOG")")

# NB: our sed rewrite of /proc/filesystems also rewrites the warning text,
# so we assert on the surviving "not readable, dynamic nodev exclusions
# disabled" tail rather than the path.
assert_contains "not readable, dynamic nodev exclusions disabled" "$(cat "$TMP/stderr")" \
	"unreadable filesystem-list must produce a warning on stderr"
assert_contains "xymonclient-linux:" "$(cat "$TMP/stderr")" \
	"warning must be tagged so it's identifiable in client logs"
assert_contains     " -x iso9660 " "$args" "iso9660 must still be excluded (EXCLUDE_TYPES default)"
assert_contains     " -x squashfs " "$args" "squashfs must still be excluded (EXCLUDE_TYPES default)"
assert_not_contains " -x sysfs "   "$args" "no nodev excludes when /proc/filesystems unreadable"

# --- a df failure with empty output must yellow, never false-green -----------
#
# Regression for the inode false-green (xymon-monitoring/xymon#96 review): a df
# that exits nonzero while printing nothing leaves an empty section the server
# reads as green ("No filesystems reporting inode data"). emit_df must surface
# ANY nonzero exit with empty output as a failure marker, not pass it silently.
# Drive this with a df stub that exits 127 while printing nothing.
REALPATH=${PATH#"$STUB:"}
FAILDIR="$TMP/bin-fail"
mkdir -p "$FAILDIR"
cat > "$FAILDIR/df" <<'EOF'
#!/usr/bin/env bash
exit 127
EOF
chmod +x "$FAILDIR/df"
ln -s "$STUB/readlink" "$FAILDIR/readlink"
for prog in sh bash awk sed; do
	p=$(PATH="$REALPATH" command -v "$prog") \
		|| fail "failing-df test needs a real '$prog' on PATH"
	ln -s "$p" "$FAILDIR/$prog"
done

fail_out=$(
	cd "$TMP"
	PATH="$FAILDIR" /bin/sh "$COMBINED" 2>/dev/null
)
assert_contains "Disk collection failed: df exited 127" "$fail_out" \
	"a nonzero df with empty output must emit a disk failure marker, not an empty section"
assert_contains "Inode collection failed: df exited 127" "$fail_out" \
	"a nonzero df with empty output must emit an inode failure marker (empty inode reads as green)"
assert_not_contains "Capacity" "$fail_out" \
	"failed-df output must not contain df headers that could parse as a healthy report"

# --- inode report drops filesystems with no inode limit (df prints "-") -------
#
# btrfs/zfs/9p/many-fuse have no fixed inode table; df -i prints "-" in the
# IUse% column (and sometimes bogus counts, e.g. a negative IUsed on 9p). Such a
# filesystem can never run out of inodes, so emit_df must drop it from the
# [inode] report -- while its disk row (a real %) stays in [df]. Drive the
# combined block with a df stub that returns a "-" inode row for "dynfs".
INODEDIR="$TMP/bin-inode"
mkdir -p "$INODEDIR"
cat > "$INODEDIR/df" <<'EOF'
#!/bin/sh
case " $* " in
  *" -i "*)
	printf 'Filesystem Inodes IUsed IFree IUse%% Mounted on\n'
	printf '/dev/sda1 1000 100 900 10%% /\n'
	printf 'dynfs 0 0 0 - /data\n'
	;;
  *)
	printf 'Filesystem 1024-blocks Used Available Capacity Mounted on\n'
	printf '/dev/sda1 1000000 500000 500000 50%% /\n'
	printf 'dynfs 2000 1000 1000 50%% /data\n'
	;;
esac
EOF
chmod +x "$INODEDIR/df"
ln -s "$STUB/readlink" "$INODEDIR/readlink"
for prog in sh bash awk sed; do
	p=$(PATH="$REALPATH" command -v "$prog") \
		|| fail "inode-drop test needs a real '$prog' on PATH"
	ln -s "$p" "$INODEDIR/$prog"
done

inode_out=$(
	cd "$TMP"
	PATH="$INODEDIR" /bin/sh "$COMBINED" 2>/dev/null
)
inode_section=$(printf '%s\n' "$inode_out" | sed -n '/^\[inode\]/,$p')
assert_contains     "/dev/sda1" "$inode_section" \
	"inode report keeps a normal filesystem"
assert_not_contains "dynfs" "$inode_section" \
	"inode report drops a no-inode-limit filesystem (IUse% '-')"
assert_contains "dynfs" "$inode_out" \
	"the same filesystem still appears in [df] (it has a real disk %)"
