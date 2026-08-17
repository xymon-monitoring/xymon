#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/client/fs-filter-linux.sh
#
# xymonclient-linux.sh under the XYMONCLIENT_FS_* contract (#49, #96), plus the
# parts that are Linux's alone: exclusions derived from /proc/filesystems, the
# rootfs exemption, and the hard-blocking types that must never reach the
# unguarded df (#316).
#
# The shared contract lives in fs-filter-common.sh and is asserted on the
# emitted [df]/[inode] sections. What this file supplies is the dialect: the
# five filesystems the fixture must contain, the row formats this client's
# post-processing expects, and the few lines that turn Linux's "-x TYPE" argv
# into the stub's answer.
#
# The [df] block is extracted from the script and run in isolation with
# /proc/filesystems and /proc/mounts repointed at fixtures -- wrapping the whole
# script would need stubs for uptime, who, vmstat, top, free, ifconfig, iostat,
# ps and more, which is more surface than the test buys back. The extraction
# pattern is the brittle part, and that trade is conscious.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
# shellcheck source=tests/client/fs-filter-common.sh
. "$(dirname "$0")/fs-filter-common.sh"

fsf_setup linux XYMONCLIENT_LINUX

# --- the dialect -------------------------------------------------------------

# Roles. sysfs is nodev, so it is excluded by the rule derived from
# /proc/filesystems; fuse.sshfs is remote but not a hard-blocking type and not a
# nodev token ("fuse" is, and matching is exact), so it is hidden by df -l alone
# -- which is what the contract's remote checks are about.
FSF_LOCAL_TYPE=ext4;        FSF_LOCAL_MP=/
FSF_PSEUDO_TYPE=sysfs;      FSF_PSEUDO_MP=/sys
FSF_NOINODE_TYPE=btrfs;     FSF_NOINODE_MP=/data
FSF_REMOTE_TYPE=fuse.sshfs; FSF_REMOTE_MP=/remote/ssh
FSF_EXTRA_TYPE=xfs;         FSF_EXTRA_MP=/extra
FSF_DECOY='sysf*'

FSF_DISK_HEADER='Filesystem 1024-blocks Used Available Capacity Mounted on'
FSF_DISK_ROW='src 1000000 500000 500000 50%% %s'
FSF_INODE_HEADER='Filesystem Inodes IUsed IFree IUse%% Mounted on'
FSF_INODE_ROW='src 1000 100 900 10%% %s'
# df prints "-" in the IUse% column for a filesystem with no inode limit;
# emit_df drops those rows on field 5.
FSF_INODE_NOLIMIT_ROW='src 0 0 0 - %s'

# Linux excludes by repeating "-x TYPE" and asks for inodes with -i.
FSF_STUB_PARSE='_prev=
for a in "$@"; do
	case "$_prev" in -x) _ex="$_ex$a " ;; esac
	case "$a" in
		-l) _local=1 ;;
		-i) _inode=1 ;;
	esac
	_prev=$a
done'

FSF_ARGV_PLAIN='-P'
FSF_ARGV_EXCLUDE_PSEUDO="-P -x $FSF_PSEUDO_TYPE"

# --- fixtures ----------------------------------------------------------------

# /proc/filesystems as a real host has it: the pseudo types that must default-
# exclude (sysfs, proc, overlay), the remote ones (nfs, nfs4, cifs -- all nodev,
# which is why surfacing them needs INCLUDE_TYPES as well as DF_LOCAL_ONLY=no),
# the real local filesystems that merely happen to be nodev and are included by
# default (tmpfs, zfs), the never-excluded rootfs, and device-backed types.
cat > "$TMP/proc.filesystems" <<'EOF'
nodev	sysfs
nodev	tmpfs
nodev	zfs
nodev	proc
nodev	overlay
nodev	fuse
nodev	nfs
nodev	nfs4
nodev	cifs
nodev	rootfs
	ext4
	xfs
	btrfs
EOF

# /proc/mounts: local only, so the remote-df sentinel stays dormant here -- it
# has its own test. Without this rewrite the LOCAL_ONLY=no case would walk the
# *tester's* mount table and probe a real cifs/ceph/glusterfs mount.
printf 'ext4\t/\n' > "$TMP/proc.mounts"
export XYMONTMP="$TMP"

# Files whose names match the type globs used below: if a configured token were
# expanded as a pathname, "sysf*" would become "sysfs" and un-exclude it.
: > "$TMP/sysfs"
: > "$TMP/fuse.sshfs"

# The OS primitives are replaced by name: the client keeps each behind a
# helper, and the fixtures below are written in the helper's contract --
# "TYPE<tab>MOUNTPOINT" for fs_mounts -- rather than in the column order of a
# file only Linux has.
SNIPPET="$TMP/df-section.sh"
fsf_extract "$SNIPPET" "" '\[inode\]'
FSF_COMBINED="$TMP/df-inode-section.sh"
fsf_extract "$FSF_COMBINED"
for _snip in "$SNIPPET" "$FSF_COMBINED"; do
	fsf_stub_helper "$_snip" fs_mounts "\tcat \"$TMP/proc.mounts\""
	fsf_stub_helper "$_snip" fs_filesystems "\tcat \"$TMP/proc.filesystems\""
done

# readlink stub: the block resolves /dev/root early on.
cat > "$STUB/readlink" <<'EOF'
#!/usr/bin/env bash
echo "/dev/sda1"
EOF
chmod +x "$STUB/readlink"

fsf_write_fixture
fsf_write_stub

# --- the contract ------------------------------------------------------------

fsf_selfcheck
fsf_contract

# --- Linux's own rules -------------------------------------------------------

run_snippet() { fsf_run "$SNIPPET" "$DF_LOG"; }

# Exclusions are derived from /proc/filesystems: every nodev type goes, except
# rootfs and the real local filesystems named by the INCLUDE_TYPES default.
args=$(run_snippet)
assert_contains " -x proc "     "$args" "a nodev pseudo type is excluded"
assert_contains " -x overlay "  "$args" "so is overlay"
assert_contains " -x iso9660 "  "$args" "the always-full images are excluded by default too"
assert_contains " -x squashfs " "$args" "including squashfs"
assert_contains " -x fuse.snapfuse " "$args" "and the FUSE spelling snapd falls back to"
assert_not_contains " -x tmpfs "  "$args" "tmpfs is nodev but real, and stays"
assert_not_contains " -x zfs "    "$args" "so does zfs"
assert_not_contains " -x rootfs " "$args" "rootfs is never excluded"
assert_not_contains " -x ext4 "   "$args" "a device-backed type is not touched"

# No hard-blocking type may reach the unguarded df. This one stays at argv
# level on purpose: it is a claim about the call, not about the report -- the
# whole point is that df must never be asked to stat these (#316).
args=$(XYMONCLIENT_FS_DF_LOCAL_ONLY=no run_snippet)
for t in nfs nfs4 cifs smb3 ceph glusterfs fuse.glusterfs lustre afs; do
	assert_contains " -x $t " "$args" \
		"DF_LOCAL_ONLY=no must keep the hard-blocking type '$t' out of the unguarded df (#316)"
done
assert_not_contains " -l " "$args" \
	"DF_LOCAL_ONLY=no must not fall back to -l: that would drop healthy remote filesystems"

# An unreadable /proc/filesystems disables only the derived exclusions.
# The helper is what fails when the list cannot be read, so that is what the
# case replaces -- a stub that returns non-zero, like the real one does on an
# unreadable /proc/filesystems.
MISSING="$TMP/df-section-missing.sh"
cp "$SNIPPET" "$MISSING"
fsf_stub_helper "$MISSING" fs_filesystems "\treturn 1"
: > "$DF_LOG"
( cd "$TMP" && /bin/sh "$MISSING" >/dev/null 2>"$TMP/stderr" ) || true
args=$(printf ' %s ' "$(cat "$DF_LOG")")
assert_contains "not readable, dynamic nodev exclusions disabled" "$(cat "$TMP/stderr")" \
	"an unreadable filesystem list must warn"
assert_contains "xymonclient-linux:" "$(cat "$TMP/stderr")" \
	"and the warning must be tagged for the client log"
assert_contains " -x iso9660 " "$args" \
	"the EXCLUDE_TYPES defaults still apply without /proc/filesystems"
assert_not_contains " -x proc " "$args" \
	"only the derived nodev exclusions are lost"

# An unreadable mount list, with DF_LOCAL_ONLY=no and a df that fails. This is
# the one path where the failure marker has two ways to be lost: the plain df's
# status is kept for the remote fallback, and the fallback itself now depends on
# a helper that can fail. A silent empty section here is a green disk column on
# a host where df collected nothing at all.
NOMOUNTS="$TMP/df-inode-section-nomounts.sh"
cp "$FSF_COMBINED" "$NOMOUNTS"
fsf_stub_helper "$NOMOUNTS" fs_mounts "\treturn 1"
_out=$(cd "$TMP" && env DF_FAIL=1 XYMONCLIENT_FS_DF_LOCAL_ONLY=no \
	/bin/sh "$NOMOUNTS" 2>/dev/null || true)
fsf_assert_loud "$_out" \
	"a df failure with no readable mount list must still be loud"

pass "xymonclient-linux.sh: the FS filter contract, nodev exclusions, and the hard-block guard -- Linux df/mount output replayed from fixtures"
