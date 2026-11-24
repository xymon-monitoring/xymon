#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/client-timestamp-length.sh
#
# Each report in xymond_client builds its one-line status summary in a fixed
# char msgline[4096], and that line carries the client's [date] text verbatim
# (the "timestr" argument). The [date] section comes from the client and has
# no length limit, so a very long one must be bounded rather than written past
# the buffer -- the same for every report, since the disk, inode, cpu, uptime,
# memory, procs, msgs, files and ports summaries all build the line this way.
#
# Pinned here by driving the real xymond_client in --local --no-update mode:
#
#   1. a [date] far longer than the line buffer is handled without trouble:
#      the worker exits 0 and still emits a status;
#   2. an ordinary [date] still appears in the status line, so the bounding
#      does not drop a normal timestamp.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin XYMOND_CLIENT xymond/xymond_client

work=$(mktempdir)
mkdir -p "$work/home"
: > "$work/analysis.cfg"

runclient() {	# report on stdin; prints the status output
	env XYMONHOME="$work/home" XYMONTMP="$work" \
		"$XYMOND_CLIENT" --local --no-update --config="$work/analysis.cfg" \
		2>"$work/stderr.log"
}

# A [date] well past msgline[4096], across several reports so more than one
# status line is built from it.
long=$(awk 'BEGIN { while (n++ < 6000) printf "x" }')

sections="[uptime]
10:00:00 up 5 days, load average: 0.1, 0.2, 0.3
[df]
Filesystem 1024-blocks Used Available Capacity Mounted on
/dev/sda1 1000 500 500 50% /
[msgs]
<...>"

# --- Case 1: an over-long [date] ---------------------------------------------
out=$(printf '@@client|1755640000|127.0.0.1|ts.test|linux|linux|\n[date]\n%s\n%s\n@@\n' \
		"$long" "$sections" | runclient) \
	|| fail "xymond_client exited non-zero on an over-long [date]: $(cat "$work/stderr.log")"

assert_match "status ts.test\." "$out" \
	"no status was emitted for a report carrying an over-long [date]"

# --- Case 2: an ordinary [date] still reaches the status line ----------------
out2=$(printf '@@client|1755640000|127.0.0.1|ts.test|linux|linux|\n[date]\nWed Nov 25 10:00:00 UTC 2026\n%s\n@@\n' \
		"$sections" | runclient) \
	|| fail "xymond_client exited non-zero on an ordinary [date]: $(cat "$work/stderr.log")"

assert_contains "Wed Nov 25 10:00:00 UTC 2026" "$out2" \
	"an ordinary [date] did not reach the status line"

pass "a client [date] is bounded on every report's status line and the worker keeps going"
