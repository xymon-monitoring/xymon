#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/client/fs-filter-freebsd.sh
#
# xymonclient-freebsd.sh under the XYMONCLIENT_FS_* contract (#170), plus what
# is FreeBSD's alone: the inode report drops zfs and tmpfs by type, because zfs
# has no inode limit at all and FreeBSD's tmpfs reports the 2^31-1 sentinel as
# itotal, which the "-" row guard cannot catch.
#
# The shared contract lives in fs-filter-common.sh and is asserted on the
# emitted sections; this file supplies the dialect -- FreeBSD excludes types
# with a single "-t no<csv>" argument. Mock-tested on any host; real FreeBSD df
# output semantics still need verifying on FreeBSD.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
# shellcheck source=tests/client/fs-filter-common.sh
. "$(dirname "$0")/fs-filter-common.sh"

fsf_setup freebsd XYMONCLIENT_FREEBSD

# --- the dialect -------------------------------------------------------------

FSF_LOCAL_TYPE=ufs;      FSF_LOCAL_MP=/
FSF_PSEUDO_TYPE=procfs;  FSF_PSEUDO_MP=/proc
FSF_NOINODE_TYPE=msdosfs; FSF_NOINODE_MP=/data
FSF_REMOTE_TYPE=nfs;     FSF_REMOTE_MP=/net
FSF_EXTRA_TYPE=ext2fs;   FSF_EXTRA_MP=/extra
FSF_DECOY='procf*'

FSF_DISK_HEADER='Filesystem Size Used Avail Capacity Mounted on'
FSF_DISK_ROW='/dev/ada0p2 100G 50G 50G 50%% %s'
FSF_INODE_HEADER='Filesystem Size Used Avail Capacity iused ifree %%iused Mounted on'
FSF_INODE_ROW='/dev/ada0p2 100 50 50 50%% 1000 9000 10%% %s'
# No inode limit: FreeBSD df prints "-" in the %iused column (field 8).
FSF_INODE_NOLIMIT_ROW='/dev/ada0p2 100 50 50 50%% 0 0 - %s'

# One "-tno<csv>" argument carries every exclusion.
FSF_STUB_PARSE='for a in "$@"; do
	case "$a" in
		-t*) _l=${a#-t}; _l=${_l#no}; _ex=" $(echo "$_l" | tr "," " ") " ;;
		-l) _local=1 ;;
		-i) _inode=1 ;;
	esac
done'

FSF_ARGV_PLAIN='-H'
FSF_ARGV_EXCLUDE_PSEUDO="-H -tno$FSF_PSEUDO_TYPE"

# --- fixtures ----------------------------------------------------------------

# A file whose name the "procf*" token would expand to, if the client let a
# configured type undergo pathname expansion.
: > "$TMP/procfs"
: > "$TMP/fuse.sshfs"

# The rest of the built-in pseudo list. The five roles observe only procfs, so
# without these a type dropped from the client's list would go unnoticed until
# it turned up as a permanently-full row on a real host.
FSF_FIXTURE_EXTRA='nullfs /pseudo/nullfs local inode
cd9660 /pseudo/cd9660 local inode
devfs /pseudo/devfs local inode
linprocfs /pseudo/linprocfs local inode
fdescfs /pseudo/fdescfs local inode
autofs /pseudo/autofs local inode'

FSF_COMBINED="$TMP/df-section.sh"
fsf_extract "$FSF_COMBINED"
fsf_write_fixture
fsf_write_stub

# --- the contract ------------------------------------------------------------

fsf_selfcheck
fsf_contract

# The built-in list, pinned through the report rather than through df's argv:
# every type it names has a filesystem in the fixture, and none may be reported.
assert_not_contains "/pseudo/" "$(fsf_section "$(fsf_report)" df)" \
	"the built-in pseudo list still excludes every type it names (nullfs cd9660 procfs devfs linprocfs fdescfs autofs)"

# --- FreeBSD's own rules -----------------------------------------------------

# The inode report drops zfs and tmpfs by type. That is an argument-level claim:
# neither filesystem is in the fixture, and the point is that the client asks df
# not to report them at all.
fsf_report >/dev/null
inode_args=$(printf ' %s ' "$(tr '\n' ' ' < "$INODE_LOG")")
disk_args=$(printf ' %s ' "$(tr '\n' ' ' < "$DF_LOG")")
assert_contains " -i " "$inode_args" "the inode report is collected with df -i"
assert_contains "zfs" "$inode_args" \
	"the inode report excludes zfs (no inode limit at all)"
assert_contains "tmpfs" "$inode_args" \
	"the inode report excludes tmpfs (itotal is the 2^31-1 sentinel, not a limit)"
assert_not_contains "tmpfs" "$disk_args" \
	"the disk report keeps tmpfs: RAM-backed capacity is real"

# ... and an admin who wants those inode rows back has the documented escape.
fsf_report XYMONCLIENT_FS_INCLUDE_TYPES=tmpfs >/dev/null
inode_args=$(printf ' %s ' "$(tr '\n' ' ' < "$INODE_LOG")")
assert_not_contains "tmpfs" "$inode_args" \
	"INCLUDE_TYPES=tmpfs un-excludes tmpfs from the inode report"

pass "xymonclient-freebsd.sh: the FS filter contract, and the zfs/tmpfs inode exclusions"
