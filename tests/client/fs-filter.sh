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
#   XYMONCLIENT_FS_DF_TIMEOUT      seconds (default 30); empty/invalid -> 30
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
# Default: the in-tree script. $XYMONCLIENT_LINUX (autopkgtest) points at the
# INSTALLED script -- /usr/lib/xymon/client/bin/xymonclient-linux.sh on Debian
# -- so the test exercises what the package actually ships, distro patches
# included. Same-version artifacts only; see "Path discovery" in
# tests/README.md.
SCRIPT="${XYMONCLIENT_LINUX:-$ROOT/client/xymonclient-linux.sh}"

# Same fail-on-explicit-override contract as require_bin (lib/assert.sh):
# $XYMONCLIENT_LINUX set means the caller asserts the installed script exists
# there, so a dangling path is a broken package layout and must not skip green.
if [ ! -f "$SCRIPT" ]; then
	[ -z "${XYMONCLIENT_LINUX:-}" ] || fail "XYMONCLIENT_LINUX explicitly set to '$SCRIPT' but no such file -- broken package layout, not a skip"
	skip "$SCRIPT missing"
fi
# The #49 env-var FS filter has landed (PR #96), so this is now a real
# regression guard: if the env-var logic ever disappears from the script the
# assertions below must FAIL, not skip. Do not reinstate a "feature not landed"
# skip here -- absence of the filter is a regression, per tests/README.md.

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

# Matching filenames expose accidental pathname expansion of configured
# filesystem type tokens when snippets run with $TMP as their working directory.
: > "$TMP/tmpfs"
: > "$TMP/fuse.sshfs"

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

# timeout stub: #49 wraps df in `timeout -s KILL <n>s df ...` (default 30s).
# timeout exec's df with the same argv, so the df stub above cannot tell
# whether it was wrapped. Record the timeout options, then exec the wrapped
# command so every df assertion above still holds with the wrapper active.
TIMEOUT_LOG="$TMP/timeout.args"
STDERR_LOG="$TMP/stderr"
cat > "$STUB/timeout" <<EOF
#!/usr/bin/env bash
echo "\$1 \$2 \$3" >> "$TIMEOUT_LOG"
shift 3
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
	: > "$STDERR_LOG"
	(
		cd "$TMP"
		/bin/sh "$SNIPPET" >/dev/null 2>"$STDERR_LOG"
	)
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
	: > "$STDERR_LOG"
	(
		cd "$TMP"
		/bin/sh "$COMBINED" >/dev/null 2>"$STDERR_LOG"
	)
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

# --- XYMONCLIENT_FS_DF_TIMEOUT wraps df in timeout with SIGKILL ---------------
#
# df can hang on stale NFS/CIFS mounts, so #49 runs it under timeout(1).
# TIMEOUT_LOG holds the options the timeout stub was handed (empty if df
# was invoked directly). Assertions read the log after each run rather than
# the returned df args, since the wrapper is invisible at the df layer.

# Default (variable unset): df is wrapped in `timeout -s KILL 30s`.
run_snippet >/dev/null
assert_equal "-s KILL 30s" "$(cat "$TIMEOUT_LOG")" \
	"default must wrap df in 'timeout -s KILL 30s'"

# A custom value is honoured verbatim.
XYMONCLIENT_FS_DF_TIMEOUT=5 run_snippet >/dev/null
assert_equal "-s KILL 5s" "$(cat "$TIMEOUT_LOG")" \
	"XYMONCLIENT_FS_DF_TIMEOUT=5 must wrap df in 'timeout -s KILL 5s'"

# Empty string falls back to the default: the cap cannot be disabled.
XYMONCLIENT_FS_DF_TIMEOUT="" run_snippet >/dev/null
assert_equal "-s KILL 30s" "$(cat "$TIMEOUT_LOG")" \
	'XYMONCLIENT_FS_DF_TIMEOUT="" must fall back to the default timeout'

# Invalid values must not prevent df from running.
XYMONCLIENT_FS_DF_TIMEOUT=invalid run_snippet >/dev/null
assert_equal "-s KILL 30s" "$(cat "$TIMEOUT_LOG")" \
	"invalid XYMONCLIENT_FS_DF_TIMEOUT must fall back to 30 seconds"
assert_contains "invalid XYMONCLIENT_FS_DF_TIMEOUT" "$(cat "$STDERR_LOG")" \
	"invalid timeout value must produce a warning"

# Zero has incompatible semantics across timeout implementations (disabled by
# GNU coreutils, immediate timeout by BusyBox), so it must use the safe default.
XYMONCLIENT_FS_DF_TIMEOUT=0 run_snippet >/dev/null
assert_equal "-s KILL 30s" "$(cat "$TIMEOUT_LOG")" \
	"XYMONCLIENT_FS_DF_TIMEOUT=0 must fall back to 30 seconds"
assert_contains "invalid XYMONCLIENT_FS_DF_TIMEOUT" "$(cat "$STDERR_LOG")" \
	"zero timeout value must produce a warning"

# Values past the 3600s ceiling are clamped: an out-of-range duration makes
# BusyBox timeout(1) exit nonzero before df runs, which would silently empty
# both the df and inode sections.
XYMONCLIENT_FS_DF_TIMEOUT=99999 run_snippet >/dev/null
assert_equal "-s KILL 3600s" "$(cat "$TIMEOUT_LOG")" \
	"timeout above 3600 must be clamped to 3600"
assert_contains "exceeds 3600" "$(cat "$STDERR_LOG")" \
	"clamped timeout must produce a warning"

# An absurdly long digit string must clamp too, without overflowing the
# shell's integer arithmetic in the range check.
XYMONCLIENT_FS_DF_TIMEOUT=18446744073709551616 run_snippet >/dev/null
assert_equal "-s KILL 3600s" "$(cat "$TIMEOUT_LOG")" \
	"oversized timeout must clamp to 3600 without an arithmetic error"

# The 3600s ceiling itself is honoured verbatim (boundary).
XYMONCLIENT_FS_DF_TIMEOUT=3600 run_snippet >/dev/null
assert_equal "-s KILL 3600s" "$(cat "$TIMEOUT_LOG")" \
	"3600 is the maximum and must pass through unchanged"

# Leading zeros are decimal, not octal, and must not trip the length-based
# clamp: 00001 is one second, not an over-length (5-char) string that the
# guard would otherwise force to 3600 -- a 3600x inflation of the operator's
# intended timeout.
XYMONCLIENT_FS_DF_TIMEOUT=00001 run_snippet >/dev/null
assert_equal "-s KILL 1s" "$(cat "$TIMEOUT_LOG")" \
	"leading-zero 00001 must normalize to 1 second, not clamp to 3600"

XYMONCLIENT_FS_DF_TIMEOUT=000030 run_snippet >/dev/null
assert_equal "-s KILL 30s" "$(cat "$TIMEOUT_LOG")" \
	"leading-zero 000030 must normalize to 30 seconds"

# A leading-zero value at/above the ceiling still clamps on magnitude.
XYMONCLIENT_FS_DF_TIMEOUT=03600 run_snippet >/dev/null
assert_equal "-s KILL 3600s" "$(cat "$TIMEOUT_LOG")" \
	"leading-zero 03600 must normalize to the 3600 boundary"

XYMONCLIENT_FS_DF_TIMEOUT=03601 run_snippet >/dev/null
assert_equal "-s KILL 3600s" "$(cat "$TIMEOUT_LOG")" \
	"leading-zero 03601 must clamp to 3600"

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

# --- timeout(1) absent: df runs unwrapped, never errors ---------------------
#
# When no timeout(1) is on PATH the script must fall back to the
# `else df "$@"` branch rather than failing. Build a PATH holding the df and
# readlink stubs plus the real interpreters/tools the snippet needs, but NO
# timeout, and confirm df still receives the full argv while TIMEOUT_LOG
# stays empty. (The default PATH always has a real /usr/bin/timeout, so this
# branch is otherwise never exercised.)
NOTIMEOUT="$TMP/bin-notimeout"
mkdir -p "$NOTIMEOUT"
ln -s "$STUB/df" "$NOTIMEOUT/df"
ln -s "$STUB/readlink" "$NOTIMEOUT/readlink"
# sh/bash run the snippet and the bash-shebang stubs; awk/sed are used inside
# the [df] block. Anything missing here would fail for a reason unrelated to
# the timeout fallback, so resolve each from the real PATH up front.
for prog in sh bash awk sed; do
	p=$(command -v "$prog") || fail "no-timeout test needs '$prog' on PATH"
	ln -s "$p" "$NOTIMEOUT/$prog"
done
: > "$DF_LOG"
: > "$TIMEOUT_LOG"
: > "$STDERR_LOG"
(
	cd "$TMP"
	PATH="$NOTIMEOUT" /bin/sh "$SNIPPET" >/dev/null 2>"$STDERR_LOG"
)
args=$(printf ' %s ' "$(cat "$DF_LOG")")
assert_contains " -x iso9660 " "$args" \
	"df must still run when timeout(1) is absent"
assert_contains " -x tmpfs " "$args" \
	"nodev excludes must still apply when timeout(1) is absent"
assert_equal "" "$(cat "$TIMEOUT_LOG")" \
	"timeout wrapper must be skipped when timeout(1) is absent"

# --- a hung df must yellow, never false-green --------------------------------
#
# Regression for the inode false-green (xymon-monitoring/xymon#96 review): when
# df hangs on a stale mount, timeout(1) kills it (exit 137 under -s KILL) and df
# emits nothing. The server reads an *empty* [inode] section as green ("No
# filesystems reporting inode data"), so the collection failure must instead
# surface as a non-empty marker line the server cannot mistake for a healthy
# report. Unlike the argv-only assertions above, this drives the REAL timeout(1)
# against a df stub that sleeps past a 1s deadline -- only the genuine kill path
# proves the right payload is emitted.
HANGDIR="$TMP/bin-hang"
mkdir -p "$HANGDIR"
# df stub that hangs: sleep well past the 1s deadline, so the header below is
# never printed and timeout must SIGKILL it.
cat > "$HANGDIR/df" <<'EOF'
#!/usr/bin/env bash
sleep 5
printf 'Filesystem 1024-blocks Used Available Capacity Mounted on\n'
EOF
chmod +x "$HANGDIR/df"
ln -s "$STUB/readlink" "$HANGDIR/readlink"
# Resolve the REAL timeout/sleep (and shells/tools) from the PATH as it was
# before $STUB was prepended -- a plain `command -v timeout` would find the
# argv-recording timeout stub above, which exec's df without enforcing any
# deadline, so df would never be killed and this test would silently pass even
# if the guard regressed.
REALPATH=${PATH#"$STUB:"}
for prog in sh bash awk sed timeout sleep; do
	p=$(PATH="$REALPATH" command -v "$prog") \
		|| fail "sleeping-df test needs a real '$prog' on PATH"
	ln -s "$p" "$HANGDIR/$prog"
done

# Run the combined [df]+[inode] block with a 1s cap; both df invocations hang
# and are killed, so the block should take ~2s and emit two markers.
hang_out=$(
	cd "$TMP"
	PATH="$HANGDIR" XYMONCLIENT_FS_DF_TIMEOUT=1 /bin/sh "$COMBINED" 2>/dev/null
)
assert_contains "Disk collection failed: df timed out" "$hang_out" \
	"a hung df must emit an explicit disk failure marker, not an empty section"
assert_contains "Inode collection failed: df timed out" "$hang_out" \
	"a hung df must emit an explicit inode failure marker (an empty inode section reads as green)"
# The marker carries none of df's column headers, so the server's header
# detection fails and the section yellows rather than greens. If "Capacity"
# leaked through, df had printed real output and the kill path was not taken.
assert_not_contains "Capacity" "$hang_out" \
	"hung-df output must not contain df headers that could parse as a healthy report"

# --- a non-kill df failure with empty output must yellow too -----------------
#
# Regression for the second inode false-green (xymon-monitoring/xymon#96
# review): the kill codes 124/137 are not the only way to get a nonzero exit
# with no output. timeout(1) itself can fail to launch df -- status 125 (timeout
# error), 126 (df not invocable), 127 (df not found) -- and a BusyBox timeout
# rejecting its duration exits nonzero before df runs. Each leaves an empty
# section the server reads as green. emit_df must surface ANY nonzero exit with
# empty output as a failure marker, not just the kill codes. Drive this with the
# argv-recording timeout stub (it exec's df, preserving df's exit status) and a
# df stub that exits 127 while printing nothing.
FAILDIR="$TMP/bin-fail"
mkdir -p "$FAILDIR"
cat > "$FAILDIR/df" <<'EOF'
#!/usr/bin/env bash
exit 127
EOF
chmod +x "$FAILDIR/df"
ln -s "$STUB/readlink" "$FAILDIR/readlink"
ln -s "$STUB/timeout"  "$FAILDIR/timeout"
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
