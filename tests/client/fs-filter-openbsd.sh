#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/client/fs-filter-openbsd.sh
#
# xymonclient-openbsd.sh under the XYMONCLIENT_FS_* contract (#170), plus what
# is OpenBSD's alone: tmpfs stays in BOTH reports, unlike FreeBSD's, because
# OpenBSD's tmpfs reports memory-derived inode counts rather than a sentinel;
# and a filesystem with no inode accounting shows itotal 0 with %iused printed
# as 100%, not "-", so the client guards on the total rather than on the text.
#
# The shared contract lives in fs-filter-common.sh and is asserted on the
# emitted sections; this file supplies the dialect -- OpenBSD excludes types
# with a single "-t no<csv>" argument. Mock-tested on any host; real OpenBSD df
# output semantics still need verifying on OpenBSD.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
# shellcheck source=tests/client/fs-filter-common.sh
. "$(dirname "$0")/fs-filter-common.sh"

fsf_setup openbsd XYMONCLIENT_OPENBSD

# --- the dialect -------------------------------------------------------------

FSF_LOCAL_TYPE=ffs;     FSF_LOCAL_MP=/
FSF_PSEUDO_TYPE=procfs; FSF_PSEUDO_MP=/proc
FSF_NOINODE_TYPE=msdos; FSF_NOINODE_MP=/data
FSF_REMOTE_TYPE=fuse; FSF_REMOTE_MP=/ssh
FSF_EXTRA_TYPE=ext2fs;  FSF_EXTRA_MP=/extra
FSF_DECOY='procf*'

FSF_DISK_HEADER='Filesystem 1K-blocks Used Avail Capacity Mounted on'
FSF_DISK_ROW='/dev/sd0a 100 50 50 50%% %s'
FSF_INODE_HEADER='Filesystem 1K-blocks Used Avail Capacity iused ifree %%iused Mounted on'
FSF_INODE_ROW='/dev/sd0a 100 50 50 50%% 1000 9000 10%% %s'
# The real shape of a filesystem with no inode accounting on OpenBSD: itotal is
# 0 (iused = ifree = 0) and %iused prints as 100%, which a text guard would read
# as "full" rather than "not applicable".
FSF_INODE_NOLIMIT_ROW='/dev/sd0a 200 1 199 1%% 0 0 100%% %s'

FSF_STUB_PARSE='for a in "$@"; do
	case "$a" in
		-t*) _l=${a#-t}; _l=${_l#no}; _ex=" $(echo "$_l" | tr "," " ") " ;;
		-l) _local=1 ;;
		-i) _inode=1 ;;
	esac
done'

FSF_ARGV_PLAIN='-k'
FSF_ARGV_EXCLUDE_PSEUDO="-k -tno$FSF_PSEUDO_TYPE"

# --- fixtures ----------------------------------------------------------------

: > "$TMP/procfs"
: > "$TMP/fuse.sshfs"

# The rest of the built-in pseudo list. The five roles observe only procfs, so
# without these a type dropped from the client's list would go unnoticed until
# it turned up as a permanently-full row on a real host.
FSF_FIXTURE_EXTRA='kernfs /pseudo/kernfs local inode
cd9660 /pseudo/cd9660 local inode'

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
	"the built-in pseudo list still excludes every type it names (kernfs procfs cd9660)"

# --- OpenBSD's own rules -----------------------------------------------------

fsf_report >/dev/null
inode_args=$(printf ' %s ' "$(tr '\n' ' ' < "$INODE_LOG")")
disk_args=$(printf ' %s ' "$(tr '\n' ' ' < "$DF_LOG")")
assert_contains " -i " "$inode_args" "the inode report is collected with df -i"
assert_not_contains "tmpfs" "$disk_args" "the disk report keeps tmpfs"
assert_not_contains "tmpfs" "$inode_args" \
	"and so does the inode report: OpenBSD's tmpfs inode counts are memory-derived"

pass "xymonclient-openbsd.sh: the FS filter contract, tmpfs in both reports, zero-inode-total drop -- OpenBSD df/mount output replayed from fixtures"
