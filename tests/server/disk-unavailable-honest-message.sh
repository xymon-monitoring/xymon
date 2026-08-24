#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/disk-unavailable-honest-message.sh
#
# An unreachable mount arrives as the client's marker row,
# "srv:/exp - - - 100% /remote/nfs": "-" sizes because nothing measured them,
# 100% so the disk column goes red on any server. Routing that 100% through
# the ordinary percent threshold made the alert text lie about the failure:
# "&red /remote/nfs (100% used) has reached the PANIC level (95%)" reads as a
# full disk, when the truth is a mount that did not answer.
#
# unix_disk_report()/unix_inode_report() must recognise the marker by its
# values ("-" in the free column plus the marker's 100%) and name the failure
# instead. Pinned here, by driving the real xymond_client in --local
# --no-update mode (statuses print to stdout instead of being sent):
#
#   - the marker row alerts "&red /remote/nfs is unreachable (not measured)"
#     in both the disk and the inode status, and no longer emits the
#     disk-full PANIC text for that mount
#   - a genuinely full filesystem (real sizes, 100%) still takes the normal
#     threshold path and reports the PANIC text -- the honest message must
#     not swallow real alerts
#   - an AIX-style all-dash row ("-" in the percent column too, as df -Ik
#     writes for /proc) is NOT mistaken for the marker: its status stays
#     green with no "unreachable" line

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin XYMOND_CLIENT xymond/xymond_client

work=$(mktempdir)
mkdir -p "$work/home"
: > "$work/analysis.cfg"

# Two client reports through one worker run: a Linux host carrying the marker
# next to a healthy and a genuinely full filesystem, then an AIX host whose
# /proc row is all dashes. XYMONHOME points at a directory of its own so the
# backfeed-queue probe fails and every generated status lands on stdout.
out=$(printf '%s' '@@client|1755640000|127.0.0.1|unix.test|linux|linux|
[df]
Filesystem 1024-blocks Used Available Capacity Mounted on
/dev/sda1 1000000 400000 600000 40% /
srv:/exp - - - 100% /remote/nfs
/dev/sdb1 1000000 1000000 0 100% /full
[inode]
Filesystem Inodes IUsed IFree IUse% Mounted on
/dev/sda1 65536 1024 64512 2% /
srv:/exp - - - 100% /remote/nfs
@@
@@client|1755640000|127.0.0.1|aix.test|aix|aix|
[df]
Filesystem 1024-blocks Used Free %Used Mounted on
/dev/hd4 1000000 400000 600000 40% /
/proc - - - - /proc
@@
' | env \
	XYMONHOME="$work/home" \
	XYMONTMP="$work" \
	"$XYMOND_CLIENT" --local --no-update --config="$work/analysis.cfg" \
	2>"$work/stderr.log") || fail "xymond_client exited non-zero: $(cat "$work/stderr.log")"

# The marker names the failure, in the disk and the inode status alike.
assert_contains "&red /remote/nfs is unreachable (not measured)" "$out" \
	"the unreachable mount is still reported as a full disk instead of being named unreachable"
assert_match "unix,test\.disk red" "$out" \
	"the disk column did not go red on the unreachable mount"
assert_match "unix,test\.inode red" "$out" \
	"the inode column did not go red on the unreachable mount"
assert_contains "ID=/remote/nfs" "$out" \
	"the inode alert for the unreachable mount lost its ID comment"

# The disk-full text is gone for the marker...
assert_not_contains "/remote/nfs (100% used)" "$out" \
	"the unreachable mount still claims '100% used' as if it were a full disk"

# ...but a genuinely full filesystem still raises it: real sizes, so the
# values say it was measured.
assert_contains "&red /full (100% used) has reached the PANIC level" "$out" \
	"a genuinely full filesystem no longer alerts through the threshold path"

# The AIX all-dash /proc row has no 100% -- it is not the marker, and must
# not start alerting.
assert_match "aix,test\.disk green" "$out" \
	"the AIX all-dash /proc row turned the disk column non-green"
assert_not_contains "/proc is unreachable" "$out" \
	"the AIX all-dash /proc row was mistaken for the unavailable-mount marker"

pass "an unreachable mount is named in the alert instead of being reported as a full disk"
