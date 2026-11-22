#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/analysis-dist-linux-ignores.sh
#
# The CLASS=linux block of the shipped analysis.cfg suppresses mounts that are
# never worth an alert. It used to name /run, /run/lock and /run/user/N one by
# one, so every other mount under /run (/run/user itself, /run/qemu, ...) went
# red as soon as its tmpfs filled; and the read-only kernel-header image WSL2
# mounts at /usr/src/linux-headers-<version>-microsoft-standard-WSL2, full by
# construction, kept disk and inode red on every WSL host.
#
# Pinned here, by driving the real xymond_client in --local --no-update mode
# with analysis.cfg.DIST and a Linux report where every mount is 100% full:
#
#   - /run and everything under it, and the WSL2 header mount, are ignored,
#     in the disk and the inode status alike
#   - /run/shm and /dev/shm stay monitored: the same space on Debian and WSL,
#     and a full one is a real incident
#   - /run/media stays monitored, in both statuses: udisks2 mounts removable
#     disks there, and /run/mediax, which only starts like it, is ignored
#   - the rules do not reach past their names: /running, and kernel headers
#     that are not WSL's, still alert

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin XYMOND_CLIENT xymond/xymond_client

ROOT=$(find_root)
dist="$ROOT/xymond/etcfiles/analysis.cfg.DIST"
[ -f "$dist" ] || skip "analysis.cfg.DIST is not in this tree"

work=$(mktempdir)
mkdir -p "$work/home"
sed "s!@XYMONHOME@!$work/home!g" "$dist" >"$work/analysis.cfg"

wsl=/usr/src/linux-headers-6.18.58.1-microsoft-standard-WSL2
out=$(printf '%s' "@@client|1755640000|127.0.0.1|wsl.test|linux|linux|
[df]
Filesystem 1024-blocks Used Available Capacity Mounted on
/dev/sdd 1000000 400000 600000 40% /
/dev/sdb 1000 1000 0 100% $wsl
/dev/sdc 1000 1000 0 100% /usr/src/linux-headers-6.1.0-13-amd64
tmpfs 1000 1000 0 100% /run
tmpfs 1000 1000 0 100% /run/lock
tmpfs 1000 1000 0 100% /run/user
tmpfs 1000 1000 0 100% /run/user/1000
tmpfs 1000 1000 0 100% /run/qemu
tmpfs 1000 1000 0 100% /run/shm
tmpfs 1000 1000 0 100% /dev/shm
/dev/sde 1000 1000 0 100% /running
/dev/sdf1 1000 1000 0 100% /run/media/alice/BACKUP
tmpfs 1000 1000 0 100% /run/mediax
[inode]
Filesystem Inodes IUsed IFree IUse% Mounted on
/dev/sdd 65536 1024 64512 2% /
/dev/sdb 2256 2256 0 100% $wsl
tmpfs 1000 1000 0 100% /run/qemu
tmpfs 1000 1000 0 100% /run/shm
/dev/sdf1 1000 1000 0 100% /run/media/alice/BACKUP
tmpfs 1000 1000 0 100% /run/mediax
@@
" | env \
	XYMONHOME="$work/home" \
	XYMONTMP="$work" \
	"$XYMOND_CLIENT" --local --no-update --config="$work/analysis.cfg" \
	2>"$work/stderr.log") || fail "xymond_client exited non-zero: $(cat "$work/stderr.log")"

for fs in "$wsl" /run /run/lock /run/user /run/user/1000 /run/qemu /run/mediax; do
	assert_not_contains "&red $fs (" "$out" \
		"$fs still alerts in the disk status; the shipped CLASS=linux rules should ignore it"
	assert_not_contains "&red <!-- ID=$fs -->" "$out" \
		"$fs still alerts in the inode status; the shipped CLASS=linux rules should ignore it"
done

for fs in /run/shm /dev/shm /running /run/media/alice/BACKUP /usr/src/linux-headers-6.1.0-13-amd64; do
	assert_contains "&red $fs (100% used)" "$out" \
		"$fs no longer alerts when full; an ignore rule reaches past the mounts it names"
done
for fs in /run/shm /run/media/alice/BACKUP; do
	assert_contains "&red <!-- ID=$fs -->" "$out" \
		"$fs no longer alerts on inodes; the /run rule must leave it monitored"
done

pass "the shipped CLASS=linux rules ignore /run but /run/shm and /run/media, and the WSL2 header mount, and nothing else"
