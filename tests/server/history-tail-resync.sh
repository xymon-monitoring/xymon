#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/history-tail-resync.sh
#
# Guard for the hist-file tail recovery in xymond_history
# (xymon-monitoring/xymon#269).
#
# When the tail of a hist file holds a valid open record followed by a
# malformed suffix (torn write, partial copy), xymond_history seeks back to
# the open record and rewrites it closed. The rewrite must also TRUNCATE the
# file after the newly written open record: without that, the stale suffix
# survives past the (shorter) rewrite, ends up glued to the new open record,
# and is carried forward onto every subsequent status change - the file never
# re-synchronizes.
#
# Drives the real worker over a hist file seeded with an open record plus a
# 300-byte garbage suffix (inside the 512-byte tail scan window) and two
# consecutive status changes, then asserts the suffix is gone and every
# record is well-formed. Skips when the worker binary has not been built.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin XYMOND_HISTORY xymond/xymond_history

WORK=$(mktempdir)
mkdir -p "$WORK/hist" "$WORK/logs" "$WORK/histlogs"
HISTFILE="$WORK/hist/junk,host.disk"

# Valid open record, then a malformed suffix shorter than the scan window.
printf 'Mon Apr  6 22:37:31 2020 green 1586205451\n' > "$HISTFILE"
dd if=/dev/zero bs=300 count=1 2>/dev/null | tr '\0' 'G' >> "$HISTFILE"

# Two consecutive status changes: green->red, then red->yellow.
printf '@@stachg#1|1586206051.00000|127.0.0.1|xymond|junk.host|disk|1586206351|red|green|1586205451|0||0|0|\nbody\n@@\n@@stachg#2|1586209651.00000|127.0.0.1|xymond|junk.host|disk|1586209951|yellow|red|1586206051|0||0|0|\nbody\n@@\n' \
	> "$WORK/msg2x"

env XYMONSERVERLOGS="$WORK/logs" XYMONHISTLOGS="$WORK/histlogs" \
	"$XYMOND_HISTORY" --histdir="$WORK/hist" < "$WORK/msg2x" > /dev/null 2>&1

result=$(cat "$HISTFILE")

# The stale suffix must not survive - not glued to a record, not anywhere.
assert_not_contains "G" "$result" \
	"stale tail bytes survived the seek-back rewrite (#269): file did not re-sync"

# Exactly the three expected records: two closed (with the durations fixed by
# the message timestamps, so timezone-independent) and the final open record.
# The open record must be the last thing in the file - nothing after it.
assert_match " green 1586205451 600
.* red 1586206051 3600
.* yellow 1586209651$" "$result" \
	"hist file does not hold the two closed records plus a clean final open record (#269)"

pass "xymond_history truncates a stale tail after seek-back rewrite (#269)"
