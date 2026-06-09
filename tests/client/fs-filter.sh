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
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
SCRIPT="$ROOT/client/xymonclient-linux.sh"

# The #49 env-var FS filter is a separate feature (PR #96) that is not yet
# merged into this branch, so its absence here is "feature not landed", not a
# regression of in-tree code -- this guard skips green until #49 arrives, then
# starts exercising the env-var logic, including the [inode] block guarded
# alongside the [df] block below.
[ -f "$SCRIPT" ] || skip "client/xymonclient-linux.sh missing"
grep -q XYMONCLIENT_FS_INCLUDE_TYPES "$SCRIPT" \
	|| skip "xymonclient-linux.sh has no #49 env-var FS filter yet (PR #96 not in this branch)"

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
# /proc/filesystems reference. Drop the trailing `echo "[inode]"` line with
# `sed '$d'` (portable last-line delete; `head -n -1` is a GNU extension and
# OpenBSD head(1) rejects a negative count) so we exit cleanly after one df
# invocation per run.
SNIPPET="$TMP/df-section.sh"
sed -n '/^echo "\[df\]"/,/^echo "\[inode\]"/p' "$SCRIPT" \
	| sed '$d' \
	| sed "s!/proc/filesystems!$TMP/proc.filesystems!g" \
	> "$SNIPPET"

# Combined [df]+[inode] block (stops before [mount], drops that trailing echo).
# The [inode] block reuses the EXCLUDES/ROOTFS computed at the top of the [df]
# block, so the two must be extracted together to run the inode invocation in
# isolation -- a lone [inode] snippet would see an empty EXCLUDES.
COMBINED="$TMP/df-inode-section.sh"
sed -n '/^echo "\[df\]"/,/^echo "\[mount\]"/p' "$SCRIPT" \
	| sed '$d' \
	| sed "s!/proc/filesystems!$TMP/proc.filesystems!g" \
	> "$COMBINED"

# --- stubs -------------------------------------------------------------------

STUB="$TMP/bin"
mkdir -p "$STUB"

# df stub: append its full argv to a log file, emit a minimal valid table
# so the downstream `sed` in the script has something to chew on. The [df] and
# [inode] blocks both shell out to df; route the inode invocation (`df -Pil` /
# `df -i`) to its own log so each block's flags can be asserted independently.
DF_LOG="$TMP/df.args"
INODE_LOG="$TMP/inode.args"
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

# timeout stub: #49 wraps df in `timeout <n>s df ...` (default 30s) so df
# can't hang forever on a stale NFS/CIFS mount. timeout exec's df with the
# same argv, so the df stub above cannot tell whether it was wrapped --
# record here the duration timeout was handed, then exec the wrapped command
# so every df assertion above still holds with the wrapper active.
TIMEOUT_LOG="$TMP/timeout.args"
cat > "$STUB/timeout" <<EOF
#!/usr/bin/env bash
echo "\$1" >> "$TIMEOUT_LOG"
shift
exec "\$@"
EOF
chmod +x "$STUB/timeout"

export PATH="$STUB:$PATH"

# --- runner ------------------------------------------------------------------

# run_snippet: invoke the extracted block in a fresh subshell, return the
# command-line df was called with. The default timeout wrapper is left in
# place (the timeout stub records it and exec's df), so the exclude/include
# assertions and the dedicated timeout assertions share one code path.
# Per-case env vars are passed via an inline prefix on the caller's
# `args=$(VAR=val run_snippet)`; see the leak warning below.
run_snippet() {
	: > "$DF_LOG"
	: > "$INODE_LOG"
	: > "$TIMEOUT_LOG"
	/bin/sh "$SNIPPET" >/dev/null 2>&1
	# Pad with spaces so substring assertions for " -x tmpfs " work even
	# at the ends of the line.
	printf ' %s ' "$(cat "$DF_LOG")"
}

# run_inode: run the combined [df]+[inode] block and return the argv the
# *inode* df invocation was called with (the df stub routes it to INODE_LOG).
# Per-case env vars are passed via an inline prefix, same as run_snippet.
run_inode() {
	: > "$DF_LOG"
	: > "$INODE_LOG"
	: > "$TIMEOUT_LOG"
	/bin/sh "$COMBINED" >/dev/null 2>&1
	printf ' %s ' "$(cat "$INODE_LOG")"
}

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
assert_contains " -x iso9660 " "$args" "default must exclude iso9660"
assert_contains " -x sysfs "  "$args" "default must exclude sysfs (nodev)"
assert_contains " -x tmpfs "  "$args" "default must exclude tmpfs (nodev)"
assert_contains " -x overlay " "$args" "default must exclude overlay (nodev)"
assert_contains " -x nfs "    "$args" "default must exclude nfs (nodev)"
assert_not_contains " -x rootfs " "$args" "default must NOT exclude rootfs (special case)"
assert_not_contains " -x ext4 " "$args" "default must NOT exclude ext4 (non-nodev)"

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

# --- XYMONCLIENT_FS_DF_LOCAL_ONLY=no drops the -l flag ----------------------

args=$(XYMONCLIENT_FS_DF_LOCAL_ONLY=no run_snippet)
assert_contains     " -P " "$args" "DF_LOCAL_ONLY=no must still pass -P"
assert_not_contains " -l " "$args" "DF_LOCAL_ONLY=no must drop -l"

args=$(XYMONCLIENT_FS_DF_LOCAL_ONLY=yes run_snippet)
assert_contains " -l " "$args" "DF_LOCAL_ONLY=yes must still pass -l"

# --- XYMONCLIENT_FS_DF_TIMEOUT wraps df in `timeout <n>s` --------------------
#
# df can hang on stale NFS/CIFS mounts, so #49 runs it under timeout(1).
# TIMEOUT_LOG holds the duration the timeout stub was handed (empty if df
# was invoked directly). Assertions read the log after each run rather than
# the returned df args, since the wrapper is invisible at the df layer.

# Default (variable unset): df is wrapped in `timeout 30s`.
run_snippet >/dev/null
assert_equal "30s" "$(cat "$TIMEOUT_LOG")" \
	"default must wrap df in 'timeout 30s'"

# A custom value is honoured verbatim.
XYMONCLIENT_FS_DF_TIMEOUT=5 run_snippet >/dev/null
assert_equal "5s" "$(cat "$TIMEOUT_LOG")" \
	"XYMONCLIENT_FS_DF_TIMEOUT=5 must wrap df in 'timeout 5s'"

# Empty string disables the wrapper: df is invoked directly, timeout untouched.
XYMONCLIENT_FS_DF_TIMEOUT="" run_snippet >/dev/null
assert_equal "" "$(cat "$TIMEOUT_LOG")" \
	'XYMONCLIENT_FS_DF_TIMEOUT="" must invoke df without a timeout wrapper'

# --- [inode] report must be filtered the same way as [df] -------------------
#
# The [inode] block runs `df -Pil -x iso9660 -x $EXCLUDES`, reusing the exact
# exclude set the [df] block built. Extracting [df] alone (the SNIPPET above)
# never exercises this second df invocation, so a regression that leaves the
# inode report unfiltered would go unnoticed. run_inode runs both blocks and
# returns the *inode* invocation's argv; assert the same nodev/rootfs/real-FS
# rules hold there.

iargs=$(run_inode)
assert_contains     " -x sysfs "  "$iargs" "inode report must exclude sysfs (nodev), same as df"
assert_contains     " -x tmpfs "  "$iargs" "inode report must exclude tmpfs (nodev), same as df"
assert_contains     " -x overlay " "$iargs" "inode report must exclude overlay (nodev), same as df"
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
