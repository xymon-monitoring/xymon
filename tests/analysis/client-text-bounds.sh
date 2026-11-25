#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/analysis/client-text-bounds.sh
#
# The client handlers in xymond/client/ and the reports xymond_client.c
# shares with every OS build their status lines in fixed arrays, from text
# the client sends: the [date] section, section names, a mount point, the
# [cpu] section, the sender. This drives the real xymond_client in --local
# --no-update mode (statuses go to stdout), one report per process, with a
# long value in each, and checks the emitted text:
#
#   - [date], a [msgs:] or [file:] section name: the status is emitted, and
#     no line carries more of the value than its 4096-byte line holds. A copy
#     that is no longer cut fails here on any build; likewise a " load="
#     value, cut to loadresult's 99 bytes;
#   - a mount point in the disk and inode reports, and the bbwin [cpu]
#     section: carried whole, and an inode line's "<!-- ID=" comment closed;
#   - a long sender: xymond_client completes and emits its statuses. The
#     sender line is not printed under --local, so only completion is
#     checked.
#
# Not tested here: the mqcollect queue and channel lines (they need an
# MQ_QUEUE or MQ_CHANNEL rule that matches) and snmpcollect (it needs a MIB
# definition); both are bounded the same way.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin XYMOND_CLIENT xymond/xymond_client

work=$(mktempdir)
mkdir -p "$work/home"
printf 'DEFAULT\n\tDISK * 90 95\n\tINODE * 90 95\n' > "$work/analysis.cfg"

b5000=$(awk 'BEGIN { while (n++ < 5000) printf "B" }')
b4096=$(awk 'BEGIN { while (n++ < 4096) printf "B" }')

# runcase NAME OS SENDER REPORT: one client report, prints what it emitted.
runcase() {
	printf '@@client|1755640000|%s|%s|%s|%s|\n%s\n@@\n' "$3" "$1" "$2" "$2" "$4" | env \
		XYMONHOME="$work/home" \
		XYMONTMP="$work" \
		"$XYMOND_CLIENT" --local --no-update --config="$work/analysis.cfg" \
		2>"$work/stderr.log" \
		|| fail "$1 ($2): xymond_client did not complete: $(tail -n 3 "$work/stderr.log")"
}

# check NAME OS WANT REPORT [SENDER]: WANT must be in the output, left in $out.
check() {
	out=$(runcase "$1" "$2" "${5:-127.0.0.1}" "$4")
	assert_contains "$3" "$out" "$1 ($2): '$3' was not emitted"
}

uptime=' 10:00:00 up 1 day, 2 users, load average: 0.10, 0.10, 0.10'

# [date] reaches every status line of a Unix client, through the reports
# shared with every OS.
check "dtlinux" linux "status dtlinux.cpu" "[date]
$b5000
[uptime]
$uptime
[msgs:/var/log/messages]
hello"
assert_not_contains "$b4096" "$out" "dtlinux: [date] was not cut to its line's 4095 bytes"

# The same for the bbwin client's own reports.
check "dtbbwin" bbwin "status dtbbwin.cpu" "[date]
$b5000
[uptime]
sec: 100
[cpu]
load=5"
assert_not_contains "$b4096" "$out" "dtbbwin: [date] was not cut to its line's 4095 bytes"

# bbwin [cpu] is added to its status as it is, so it is carried whole.
check "cpubbwin" bbwin "status cpubbwin.cpu" "[date]
Wed Nov 25 07:00:00 UTC 2026
[uptime]
sec: 100
[cpu]
load=5 $b5000"
assert_contains "$b5000" "$out" "cpubbwin: [cpu] was not carried whole"

# A [msgs:] and a [file:] section name are cut to their line.
check "msname" linux "status msname.msgs" "[date]
Wed Nov 25 07:00:00 UTC 2026
[msgs:$b5000]
hello"
assert_not_contains "$b4096" "$out" "msname: a [msgs:] name was not cut to its line's 4095 bytes"
check "flname" linux "status flname.files" "[date]
Wed Nov 25 07:00:00 UTC 2026
[file:/$b5000]
type:100000 (file)
mode:644 (-rw-r--r--)
size:10"
assert_not_contains "$b4096" "$out" "flname: a [file:] name was not cut to its line's 4095 bytes"

# A mount point is carried whole in a disk and an inode alert, and the
# inode line's comment is closed.
check "mtlinux" linux "status mtlinux.inode" "[date]
Wed Nov 25 07:00:00 UTC 2026
[df]
Filesystem 1024-blocks Used Available Capacity Mounted on
/dev/sda1 1000 990 10 99% /$b5000
[inode]
Filesystem Inodes IUsed IFree IUse% Mounted on
/dev/sda1 1000 990 10 99% /$b5000"
assert_contains "&red /$b5000 (99% used) has reached the PANIC level" "$out" \
	"mtlinux: the disk alert did not carry the mount point whole"
assert_contains "&red <!-- ID=/$b5000 --> /$b5000 (99% used) has reached the PANIC level" "$out" \
	"mtlinux: the inode alert did not carry the mount point whole, with its comment closed"

# The " load=" value of an [uptime] without a load average is cut to
# loadresult's 99 bytes.
d3000=$(awk 'BEGIN { while (n++ < 3000) printf "9" }')
d100=$(awk 'BEGIN { while (n++ < 100) printf "9" }')
check "ldlinux" linux "status ldlinux.cpu" "[date]
Wed Nov 25 07:00:00 UTC 2026
[uptime]
 10:00:00 up 1 day, 2 users, load=$d3000"
assert_not_contains "load=$d100" "$out" "ldlinux: the load= value was not cut to loadresult's 99 bytes"

# A long sender under --local: the handlers write it into fromline[1024].
check "sndlinux" linux "status sndlinux.cpu" "[date]
Wed Nov 25 07:00:00 UTC 2026
[uptime]
$uptime" "$b5000"

pass "the client handlers and the reports they share bound the client's text, and carry whole what they must"
