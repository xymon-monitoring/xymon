#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/client/fs-filter.sh
#
# Regression guard for xymon-monitoring/xymon#96 (refs #49):
# xymonclient-linux.sh respects four environment variables that tune
# which filesystems appear in the df/inode blocks of the client report:
#
#   XYMONCLIENT_FS_INCLUDE_TYPES   types to un-exclude (e.g. tmpfs)
#   XYMONCLIENT_FS_EXCLUDE_TYPES   additional types to exclude
#   XYMONCLIENT_FS_DF_LOCAL_ONLY   yes (default, df -l) vs no
#   XYMONCLIENT_FS_DF_TIMEOUT      seconds (default 30) or "" to disable
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
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
SCRIPT="$ROOT/client/xymonclient-linux.sh"

# Skip cleanly if we're on a branch that doesn't carry the env-var
# handling yet -- there's nothing meaningful to assert against the
# legacy script and we don't want to false-fail on PR branches that
# haven't merged #49 yet.
[ -f "$SCRIPT" ] || skip "client/xymonclient-linux.sh missing"
grep -q XYMONCLIENT_FS_INCLUDE_TYPES "$SCRIPT" \
	|| skip "xymonclient-linux.sh on this branch predates #49 env-var FS filter"

TMP=$(mktempdir)

# --- fixtures ----------------------------------------------------------------

# Representative /proc/filesystems: pseudo-FS that should default-exclude
# (sysfs, tmpfs, proc, overlay, nfs, nfs4), one that must never be excluded
# (rootfs), and two real disk FS that must not appear in the nodev list.
cat > "$TMP/proc.filesystems" <<'EOF'
nodev	sysfs
nodev	tmpfs
nodev	proc
	ext4
	xfs
nodev	overlay
nodev	nfs
nodev	nfs4
nodev	rootfs
EOF

# Extract just the [df] block from xymonclient-linux.sh and rewrite the
# /proc/filesystems reference. Drop the trailing `echo "[inode]"` line
# (head -n -1) so we exit cleanly after one df invocation per run.
SNIPPET="$TMP/df-section.sh"
sed -n '/^echo "\[df\]"/,/^echo "\[inode\]"/p' "$SCRIPT" \
	| head -n -1 \
	| sed "s!/proc/filesystems!$TMP/proc.filesystems!g" \
	> "$SNIPPET"

# --- stubs -------------------------------------------------------------------

STUB="$TMP/bin"
mkdir -p "$STUB"

# df stub: append its full argv to a log file, emit a minimal valid table
# so the downstream `sed` in the script has something to chew on.
DF_LOG="$TMP/df.args"
cat > "$STUB/df" <<EOF
#!/usr/bin/env bash
echo "\$*" >> "$DF_LOG"
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

export PATH="$STUB:$PATH"

# --- runner ------------------------------------------------------------------

# run_snippet: invoke the extracted block in a fresh subshell, return the
# command-line df was called with. XYMONCLIENT_FS_DF_TIMEOUT="" is set so
# df is called directly (without the `timeout 30s` wrapper) -- the test
# is about exclude/include semantics, not the timeout wrapper.
run_snippet() {
	: > "$DF_LOG"
	XYMONCLIENT_FS_DF_TIMEOUT="" /bin/sh "$SNIPPET" >/dev/null 2>&1
	# Pad with spaces so substring assertions for " -x tmpfs " work even
	# at the ends of the line.
	printf ' %s ' "$(cat "$DF_LOG")"
}

# --- default behaviour: -P -l, every nodev (except rootfs) excluded ---------

args=$(run_snippet)

assert_contains " -P "        "$args" "default must pass -P to df"
assert_contains " -l "        "$args" "default must pass -l to df (local only)"
assert_contains " -x iso9660 " "$args" "default must exclude iso9660"
assert_contains " -x sysfs "  "$args" "default must exclude sysfs (nodev)"
assert_contains " -x tmpfs "  "$args" "default must exclude tmpfs (nodev)"
assert_contains " -x overlay " "$args" "default must exclude overlay (nodev)"
assert_contains " -x nfs "    "$args" "default must exclude nfs (nodev)"
assert_not_contains " -x rootfs " "$args" "default must NOT exclude rootfs (special case)"
assert_not_contains " -x ext4 " "$args" "default must NOT exclude ext4 (non-nodev)"

# --- XYMONCLIENT_FS_INCLUDE_TYPES un-excludes one type ----------------------

XYMONCLIENT_FS_INCLUDE_TYPES=tmpfs args=$(XYMONCLIENT_FS_INCLUDE_TYPES=tmpfs run_snippet)
assert_not_contains " -x tmpfs " "$args" \
	"XYMONCLIENT_FS_INCLUDE_TYPES=tmpfs must drop tmpfs from -x list"
assert_contains " -x sysfs " "$args" \
	"XYMONCLIENT_FS_INCLUDE_TYPES=tmpfs must not affect other excludes"

# Multiple include types in one call.
args=$(XYMONCLIENT_FS_INCLUDE_TYPES="tmpfs overlay" run_snippet)
assert_not_contains " -x tmpfs "   "$args" "multi-include must drop tmpfs"
assert_not_contains " -x overlay " "$args" "multi-include must drop overlay"
assert_contains     " -x sysfs "   "$args" "multi-include must leave sysfs"

# --- XYMONCLIENT_FS_EXCLUDE_TYPES adds extra exclusions ---------------------

args=$(XYMONCLIENT_FS_EXCLUDE_TYPES=ext4 run_snippet)
assert_contains " -x ext4 "  "$args" "XYMONCLIENT_FS_EXCLUDE_TYPES=ext4 must add ext4"
assert_contains " -x sysfs " "$args" "extra exclude must not displace defaults"

# Extra exclude that's already in the default list must not be duplicated.
args=$(XYMONCLIENT_FS_EXCLUDE_TYPES=tmpfs run_snippet)
tmpfs_count=$(printf '%s' "$args" | grep -o -- '-x tmpfs' | wc -l)
assert_equal 1 "$tmpfs_count" \
	"XYMONCLIENT_FS_EXCLUDE_TYPES=tmpfs must not duplicate an existing exclude"

# --- XYMONCLIENT_FS_DF_LOCAL_ONLY=no drops the -l flag ----------------------

args=$(XYMONCLIENT_FS_DF_LOCAL_ONLY=no run_snippet)
assert_contains     " -P " "$args" "DF_LOCAL_ONLY=no must still pass -P"
assert_not_contains " -l " "$args" "DF_LOCAL_ONLY=no must drop -l"

args=$(XYMONCLIENT_FS_DF_LOCAL_ONLY=yes run_snippet)
assert_contains " -l " "$args" "DF_LOCAL_ONLY=yes must still pass -l"

# --- /proc/filesystems missing: warn, no excludes (script must not crash) ---

# Repoint the snippet at a non-existent path; the [-r ...] guard should
# kick in, the warning go to stderr, and df be invoked without any nodev
# -x flags (iso9660 is always passed).
MISSING_SNIPPET="$TMP/df-section-missing.sh"
sed "s!$TMP/proc.filesystems!$TMP/does-not-exist!g" "$SNIPPET" > "$MISSING_SNIPPET"

: > "$DF_LOG"
XYMONCLIENT_FS_DF_TIMEOUT="" /bin/sh "$MISSING_SNIPPET" >/dev/null 2>"$TMP/stderr"
args=$(printf ' %s ' "$(cat "$DF_LOG")")

# NB: our sed rewrite of /proc/filesystems also rewrites the warning text,
# so we assert on the surviving "not readable, filesystem filter disabled"
# tail rather than the path.
assert_contains "not readable, filesystem filter disabled" "$(cat "$TMP/stderr")" \
	"unreadable filesystem-list must produce a warning on stderr"
assert_contains "xymonclient-linux:" "$(cat "$TMP/stderr")" \
	"warning must be tagged so it's identifiable in client logs"
assert_contains     " -x iso9660 " "$args" "iso9660 must still be excluded"
assert_not_contains " -x sysfs "   "$args" "no nodev excludes when /proc/filesystems unreadable"
