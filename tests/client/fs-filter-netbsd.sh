#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/client/fs-filter-netbsd.sh
#
# xymonclient-netbsd.sh under the XYMONCLIENT_FS_* contract (#170), plus what is
# NetBSD's alone: zfs is excluded from the inode report by type, tmpfs is kept
# (its inode counts are real), and the raw df -i columns are normalised into the
# shared parser's layout before they reach the server.
#
# The shared contract lives in fs-filter-common.sh and is asserted on the
# emitted sections; this file supplies the dialect -- NetBSD excludes types with
# a single "-t no<csv>" argument.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
# shellcheck source=tests/client/fs-filter-common.sh
. "$(dirname "$0")/fs-filter-common.sh"

fsf_setup netbsd XYMONCLIENT_NETBSD
SERVER=$(find_root)/xymond/client/netbsd.c

# --- the dialect -------------------------------------------------------------

FSF_LOCAL_TYPE=ffs;     FSF_LOCAL_MP=/
FSF_PSEUDO_TYPE=procfs; FSF_PSEUDO_MP=/proc
FSF_NOINODE_TYPE=msdos; FSF_NOINODE_MP=/data
FSF_REMOTE_TYPE=psshfs; FSF_REMOTE_MP=/ssh
FSF_EXTRA_TYPE=ext2fs;  FSF_EXTRA_MP=/extra
FSF_DECOY='procf*'

FSF_DISK_HEADER='Filesystem 512-blocks Used Available Capacity Mounted on'
FSF_DISK_ROW='/dev/ld0a 3837980 2953852 692232 81%% %s'
FSF_INODE_HEADER='Filesystem 512-blocks Used Available Capacity iUsed iAvail %%iCap Mounted on'
FSF_INODE_ROW='/dev/ld0a 3837980 2953852 692232 81%% 41013 218057 15%% %s'
# No inode accounting: iUsed and iAvail are both 0, which is what the client
# guards on (it cannot use the "-" text NetBSD does not print).
FSF_INODE_NOLIMIT_ROW='/dev/ld0a 1048576 8 1048568 0%% 0 0 0%% %s'

FSF_STUB_PARSE='for a in "$@"; do
	case "$a" in
		-t*) _l=${a#-t}; _l=${_l#no}; _ex=" $(echo "$_l" | tr "," " ") " ;;
		-l) _local=1 ;;
		-i) _inode=1 ;;
	esac
done'

FSF_ARGV_PLAIN='-P'
FSF_ARGV_EXCLUDE_PSEUDO="-P -tno$FSF_PSEUDO_TYPE"

# --- fixtures ----------------------------------------------------------------

: > "$TMP/procfs"
: > "$TMP/fuse.sshfs"

# The rest of the built-in pseudo list. The five roles observe only procfs, so
# without these a type dropped from the client's list would go unnoticed until
# it turned up as a permanently-full row on a real host.
FSF_FIXTURE_EXTRA='kernfs /pseudo/kernfs local inode
cd9660 /pseudo/cd9660 local inode
null /pseudo/null local inode
ptyfs /pseudo/ptyfs local inode'

FSF_COMBINED="$TMP/df-section.sh"
fsf_extract "$FSF_COMBINED"
fsf_write_fixture
fsf_write_stub
# The client reads the mount list on every DF_LOCAL_ONLY=no cycle now that
# hard-blocking mounts are guarded (#316); answer it from the fixture rather
# than from the tester's machine.
# NetBSD and OpenBSD write the type after the mount point, not inside the
# parentheses -- measured on the lane VMs, where the FreeBSD spelling made
# fs_mounts() drop every row.
FSF_MOUNT_FMT='src on %s type %s (%s)\n'
fsf_write_mount_stub

# --- the contract ------------------------------------------------------------

fsf_selfcheck
fsf_contract

# The built-in list, pinned through the report rather than through df's argv:
# every type it names has a filesystem in the fixture, and none may be reported.
assert_not_contains "/pseudo/" "$(fsf_section "$(fsf_report)" df)" \
	"the built-in pseudo list still excludes every type it names (kernfs procfs cd9660 null ptyfs)"

# --- NetBSD's own rules ------------------------------------------------------

out=$(fsf_report)
inode_args=$(printf ' %s ' "$(tr '\n' ' ' < "$INODE_LOG")")
assert_contains " -i " "$inode_args" "the inode report is collected with df -i"
assert_contains " -l " "$inode_args" "the inode report shares the local-only behaviour"
assert_contains "zfs" "$inode_args" "the inode report adds the zfs exclusion"
assert_not_contains "tmpfs" "$inode_args" \
	"NetBSD tmpfs inode counts are real and stay reportable"

# The raw df -i columns are rewritten into the layout the shared server parser
# reads, with itotal computed as iUsed + iAvail.
inode_section=$(fsf_section "$out" inode)
assert_contains "Filesystem itotal iused ifree %iused Mounted on" "$inode_section" \
	"the inode output exposes the shared parser's columns"
assert_contains "/dev/ld0a 259070 41013 218057 15% /" "$inode_section" \
	"the inode output normalises NetBSD's df values"

server_source=$(<"$SERVER")
assert_contains 'inodestr = getdata("inode");' "$server_source" \
	"the NetBSD handler reads the inode section"
assert_contains 'unix_inode_report(hostname' "$server_source" \
	"the NetBSD handler generates the inode status"

pass "xymonclient-netbsd.sh: the FS filter contract, the zfs inode exclusion, and column normalisation -- NetBSD df/mount output replayed from fixtures"
