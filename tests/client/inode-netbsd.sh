#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later

set -euo pipefail

. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
SCRIPT=${XYMONCLIENT_NETBSD:-$ROOT/client/xymonclient-netbsd.sh}
SERVER=$ROOT/xymond/client/netbsd.c

[ -f "$SCRIPT" ] || skip "NetBSD client script not found: $SCRIPT"
[ -f "$SERVER" ] || skip "NetBSD server handler not found: $SERVER"

TMP=$(mktempdir)
mkdir -p "$TMP/bin"

cat > "$TMP/bin/df" <<EOF
#!/usr/bin/env bash
printf '%s\n' "\$*" > "$TMP/df.args"
printf 'Filesystem 512-blocks Used Available Capacity iUsed iAvail %%iCap Mounted on\n'
printf '/dev/ld0a 3837980 2953852 692232 81%% 41013 218057 15%% /\n'
printf 'ptyfs 2 2 0 100%% 0 0 0%% /dev/pts\n'
EOF
chmod +x "$TMP/bin/df"

sed -n '/^echo "\[df\]"$/,/^echo "\[inode\]"$/p' "$SCRIPT" | sed '$d' > "$TMP/df.sh"
sed -n '/^echo "\[inode\]"$/,/^echo "\[mount\]"$/p' "$SCRIPT" | sed '$d' > "$TMP/inode.sh"
[ -s "$TMP/df.sh" ] || fail "NetBSD client df block not found"
[ -s "$TMP/inode.sh" ] || fail "NetBSD client inode block not found"

PATH="$TMP/bin:$PATH" /bin/sh "$TMP/df.sh" >/dev/null
disk_args=$(<"$TMP/df.args")
assert_equal "-P -tnonfs,kernfs,procfs,cd9660,null,ptyfs" "$disk_args" \
	"disk collection excludes ptyfs and other unsupported filesystems"

output=$(PATH="$TMP/bin:$PATH" /bin/sh "$TMP/inode.sh")
args=$(<"$TMP/df.args")

assert_equal "-i -tnonfs,kernfs,procfs,cd9660,null,ptyfs" "$args" \
	"inode collection excludes unsupported and pseudo filesystems"
assert_contains "Filesystem itotal iused ifree %iused Mounted on" "$output" \
	"inode output exposes the shared parser's expected columns"
assert_contains "/dev/ld0a 259070 41013 218057 15% /" "$output" \
	"inode output derives total, used, and free counts from NetBSD df"

server_source=$(<"$SERVER")
assert_contains 'inodestr = getdata("inode");' "$server_source" \
	"NetBSD handler reads the inode client section"
assert_contains 'unix_inode_report(hostname' "$server_source" \
	"NetBSD handler generates the inode status report"

pass "NetBSD inode collection and server wiring"
