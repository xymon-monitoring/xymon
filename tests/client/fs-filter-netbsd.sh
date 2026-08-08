#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
# shellcheck source=tests/client/fs-filter-common.sh
. "$(dirname "$0")/fs-filter-common.sh"

fsf_setup netbsd XYMONCLIENT_NETBSD
SERVER=$(find_root)/xymond/client/netbsd.c

cat > "$STUB/df" <<EOF
#!/usr/bin/env bash
[ -n "\${DF_FAIL:-}" ] && exit 1
if [[ " \$* " =~ " -i " ]]; then
	echo "\$*" >> "$INODE_LOG"
	printf 'Filesystem 512-blocks Used Available Capacity iUsed iAvail %%iCap Mounted on\n'
	printf '/dev/ld0a 3837980 2953852 692232 81%% 41013 218057 15%% /\n'
	printf 'tmpfs 1048576 8 1048568 0%% 1 999999 0%% /tmp\n'
else
	echo "\$*" >> "$DF_LOG"
	printf 'Filesystem 512-blocks Used Available Capacity Mounted on\n'
	printf '/dev/ld0a 3837980 2953852 692232 81%% /\n'
fi
EOF
chmod +x "$STUB/df"

: > "$TMP/procfs"
: > "$TMP/fuse.sshfs"

SNIPPET="$TMP/df-section.sh"
fsf_extract "$SNIPPET"
run() { fsf_run "$SNIPPET" "$DF_LOG"; }

args=$(run)
assert_contains " -P " "$args" "disk df keeps -P"
assert_contains " -l " "$args" "default is local-only"
assert_contains " -tnokernfs,procfs,cd9660,null,ptyfs " "$args" \
	"default excludes pseudo filesystems"
assert_not_contains "nonfs" "$args" "nfs is controlled by df -l"

inode_args=$(printf ' %s ' "$(tr '\n' ' ' < "$INODE_LOG")")
assert_contains " -i " "$inode_args" "inode df uses -i"
assert_contains " -l " "$inode_args" "inode df shares local-only behavior"
assert_contains " -tnokernfs,procfs,cd9660,null,ptyfs " "$inode_args" \
	"inode df shares type exclusions"
assert_not_contains "tmpfs" "$inode_args" \
	"NetBSD tmpfs inode counts remain reportable"

args=$(XYMONCLIENT_FS_INCLUDE_TYPES=ptyfs run)
assert_not_contains "ptyfs" "$args" "INCLUDE_TYPES un-excludes a default type"
inode_args=$(printf ' %s ' "$(tr '\n' ' ' < "$INODE_LOG")")
assert_not_contains "ptyfs" "$inode_args" \
	"INCLUDE_TYPES applies to inode collection"

args=$(XYMONCLIENT_FS_EXCLUDE_TYPES=ext2fs run)
assert_contains "ext2fs" "$args" "EXCLUDE_TYPES adds a filesystem type"

args=$(XYMONCLIENT_FS_INCLUDE_TYPES=ext2fs XYMONCLIENT_FS_EXCLUDE_TYPES=ext2fs run)
assert_contains "ext2fs" "$args" "EXCLUDE_TYPES wins over INCLUDE_TYPES"

args=$(XYMONCLIENT_FS_INCLUDE_TYPES='procf*' run)
assert_contains ",procfs," "$args" "include tokens do not undergo glob expansion"

args=$(XYMONCLIENT_FS_EXCLUDE_TYPES='fuse.*' run)
assert_contains "fuse.*" "$args" "exclude tokens do not undergo glob expansion"
assert_not_contains "fuse.sshfs" "$args" "exclude globs remain literal"

args=$(XYMONCLIENT_FS_DF_LOCAL_ONLY=no run)
assert_not_contains " -l " "$args" "DF_LOCAL_ONLY=no reports remote filesystems"
args=$(XYMONCLIENT_FS_DF_LOCAL_ONLY=invalid run)
assert_contains " -l " "$args" "invalid DF_LOCAL_ONLY falls back to local-only"
assert_contains "invalid XYMONCLIENT_FS_DF_LOCAL_ONLY" "$(cat "$STDERR_LOG")" \
	"invalid DF_LOCAL_ONLY warns"

output=$(cd "$TMP" && /bin/sh "$SNIPPET" 2>/dev/null)
assert_contains "Filesystem itotal iused ifree %iused Mounted on" "$output" \
	"inode output exposes the shared parser columns"
assert_contains "/dev/ld0a 259070 41013 218057 15% /" "$output" \
	"inode output normalizes NetBSD df values"
assert_contains "tmpfs 1000000 1 999999 0% /tmp" "$output" \
	"inode output retains meaningful NetBSD tmpfs values"

output=$(cd "$TMP" && DF_FAIL=1 /bin/sh "$SNIPPET" 2>/dev/null)
assert_contains "Disk report collection failed" "$output" \
	"a failed disk probe emits a failure marker"
assert_contains "Inode report collection failed" "$output" \
	"a failed inode probe emits a failure marker"
assert_not_contains "Filesystem" "$output" \
	"failure markers have no healthy df header"

server_source=$(<"$SERVER")
assert_contains 'inodestr = getdata("inode");' "$server_source" \
	"NetBSD handler reads the inode section"
assert_contains 'unix_inode_report(hostname' "$server_source" \
	"NetBSD handler generates the inode status"

pass "NetBSD filesystem filtering, inode collection, and failure reporting"