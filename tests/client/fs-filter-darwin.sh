#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/client/fs-filter-darwin.sh
#
# xymonclient-darwin.sh under the XYMONCLIENT_FS_* contract (#170), plus what is
# macOS's alone. macOS df has no type filter, so the client builds the list from
# mount(8) and runs one df per path: the "types" it filters on are the tokens in
# mount's parentheses, which are attributes (nobrowse, read-only) as much as
# filesystem types. Modern macOS seals / read-only and flags every system volume
# nobrowse -- including the data volume holding all user files, which Apple
# marks "root data" and which the client exempts, so the one volume that can
# actually fill is always reported.
#
# The shared contract lives in fs-filter-common.sh and is asserted on the
# emitted sections. Fixture measured on a real Apple-silicon Mac (2026-07). The
# snippet uses IFS=$'\n', which the macOS /bin/sh (bash) supports but dash does
# not, so it runs under bash here, matching macOS.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
# shellcheck source=tests/client/fs-filter-common.sh
. "$(dirname "$0")/fs-filter-common.sh"

fsf_setup darwin XYMONCLIENT_DARWIN
command -v column >/dev/null 2>&1 || skip "column(1) not available"

FSF_SHELL=bash

# --- the dialect -------------------------------------------------------------

# The default-excluded "type" is an attribute: nobrowse. apfs plays the
# no-inode-limit role -- its ifree is container-derived and identical across
# volumes, so it is excluded from the inode report while its disk row stays.
FSF_LOCAL_TYPE=hfs;      FSF_LOCAL_MP=/Volumes/USBHFS
FSF_PSEUDO_TYPE=nobrowse; FSF_PSEUDO_MP=/System/Volumes/VM
FSF_NOINODE_TYPE=apfs;   FSF_NOINODE_MP=/Volumes/External
FSF_REMOTE_TYPE=nfs;     FSF_REMOTE_MP=/Volumes/nfs
FSF_EXTRA_TYPE=exfat;    FSF_EXTRA_MP=/Volumes/Extra
FSF_DECOY='nobrow*'

FSF_DISK_HEADER='Filesystem 1024-blocks Used Available Capacity Mounted on'
FSF_DISK_ROW='/dev/disk 100 50 50 50%% %s'
FSF_INODE_HEADER='Filesystem 1024-blocks Used Available Capacity iused ifree %%iused Mounted on'
FSF_INODE_ROW='/dev/disk 100 50 50 50%% 1000 9000 10%% %s'
# Unused here: apfs never reaches an inode df at all, it is excluded by type.
FSF_INODE_NOLIMIT_ROW='/dev/disk 100 50 50 50%% 0 0 0%% %s'

# df is asked about one path at a time; there is no exclusion argument.
FSF_STUB_PARSE='for a in "$@"; do
	case "$a" in
		-i) _inode=1 ;;
		/*) _named="$_named $a" ;;
	esac
done'

FSF_ARGV_PLAIN="-P -H $FSF_LOCAL_MP"
FSF_ARGV_EXCLUDE_PSEUDO=''	# no type filter in df: see fsf_selfcheck

# --- fixtures ----------------------------------------------------------------

# mount(8) as a real Apple-silicon Mac prints it: sealed read-only root, every
# system volume nobrowse, the data volume marked "root data", plus the fixture's
# five roles. Remote mounts simply lack the "local" attribute.
cat > "$STUB/mount" <<EOF
#!/usr/bin/env bash
cat <<'MOUNT'
/dev/disk3s1s1 on / (apfs, sealed, local, read-only, journaled)
devfs on /dev (devfs, local, nobrowse)
/dev/disk3s5 on /System/Volumes/Data (apfs, local, journaled, nobrowse, protect, root data)
map auto_home on /System/Volumes/Data/home (autofs, automounted, nobrowse)
/dev/disk3s6 on $FSF_PSEUDO_MP (apfs, local, noexec, journaled, noatime, nobrowse)
/dev/disk6s1 on $FSF_LOCAL_MP ($FSF_LOCAL_TYPE, local, nodev, nosuid, journaled)
/dev/disk5s1 on $FSF_NOINODE_MP ($FSF_NOINODE_TYPE, local, nodev, nosuid, journaled)
/dev/disk8s1 on $FSF_EXTRA_MP ($FSF_EXTRA_TYPE, local, nodev, nosuid)
remote:/exp on $FSF_REMOTE_MP ($FSF_REMOTE_TYPE, nodev, nosuid)
MOUNT
EOF
chmod +x "$STUB/mount"

# A file the "nobrow*" token would expand to if a configured token were treated
# as a pathname rather than a literal.
: > "$TMP/nobrowse"
: > "$TMP/hfs"

# The volumes above that are not one of the five roles: macOS asks df about
# every path it keeps, so each must be answerable.
FSF_FIXTURE_EXTRA='apfs / local inode
apfs /System/Volumes/Data local inode
devfs /dev local inode
autofs /System/Volumes/Data/home local inode'

FSF_COMBINED="$TMP/df-section.sh"
fsf_extract "$FSF_COMBINED"
fsf_write_fixture
fsf_write_stub

# --- the contract ------------------------------------------------------------

fsf_selfcheck
fsf_contract

# --- macOS's own rules -------------------------------------------------------

out=$(fsf_report)
df_section=$(fsf_section "$out" df)

# The data volume survives its nobrowse flag through the "root data" exemption;
# the sealed read-only root cannot fill and is dropped.
assert_contains "/System/Volumes/Data" "$df_section" \
	"the root-data exemption keeps the data volume despite nobrowse"
assert_not_contains "/System/Volumes/Data/home" "$df_section" \
	"the automounted map is dropped (nobrowse, and not root data)"
disk_args=$(printf ' %s ' "$(tr '\n' ' ' < "$DF_LOG")")
assert_not_contains " / " "$disk_args" "the sealed read-only root is dropped: it cannot fill"

# An explicit exclude beats the exemption: the admin said apfs goes.
out=$(fsf_report XYMONCLIENT_FS_EXCLUDE_TYPES=apfs)
assert_not_contains "/System/Volumes/Data" "$(fsf_section "$out" df)" \
	"EXCLUDE_TYPES=apfs beats the root-data exemption"

# INCLUDE_TYPES=read-only surfaces the sealed root, but not the nobrowse image.
out=$(fsf_report XYMONCLIENT_FS_INCLUDE_TYPES=read-only)
disk_args=$(printf ' %s ' "$(tr '\n' ' ' < "$DF_LOG")")
assert_contains " / " "$disk_args" "INCLUDE_TYPES=read-only surfaces the sealed root"

# The inode report queries every kept filesystem in inode mode -- never disk
# mode. The loop body once used "df -P -H" for the second filesystem onwards,
# which mislabelled disk data as inode data.
out=$(fsf_report)
inode_args=$(printf ' %s ' "$(tr '\n' ' ' < "$INODE_LOG")")
assert_contains " -i " "$inode_args" "the inode report uses df -i"
assert_not_contains " -H " "$inode_args" \
	"the inode report must not use df -H: that mislabels disk data as inode data"
assert_not_contains "$FSF_NOINODE_MP" "$inode_args" \
	"apfs volumes are never queried for inodes (container-derived ifree)"
# Counted, not just inspected: the bad loop sent the 2nd filesystem onwards to
# "df -P -H", which lands in the disk log and leaves nothing wrong to see here.
assert_equal 2 "$(grep -c . "$INODE_LOG")" \
	"every inode-capable filesystem is queried in inode mode, not just the first"
assert_contains "$FSF_EXTRA_MP" "$(fsf_section "$out" inode)" \
	"and the second one's row reaches the inode report"

# An all-apfs Mac has nothing inode-limited: that is empty DATA, not a failure.
: > "$INODE_LOG"
out=$(fsf_report "XYMONCLIENT_FS_EXCLUDE_TYPES=$FSF_LOCAL_TYPE $FSF_EXTRA_TYPE")
assert_not_contains "collection failed" "$(fsf_section "$out" inode)" \
	"an all-apfs inode list is empty data, not a failure"
assert_equal "" "$(cat "$INODE_LOG")" "and no inode df runs at all"
assert_contains "/System/Volumes/Data" "$out" "while the disk report still carries the data volume"

pass "xymonclient-darwin.sh: the FS filter contract, the root-data exemption, the apfs-free inode report"
