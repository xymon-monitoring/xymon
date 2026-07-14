#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/disk-linecount-hint.sh
#
# unix_disk_report()/unix_inode_report() emit "<!-- linecount=N -->" into the
# disk/inode status, N = the number of filesystems shown (one RRD file each),
# so the status page pages the graph precisely instead of re-counting df
# lines. The functions live in the xymond_client daemon and only output
# through the message channel, so - like svcstatus-trends-overflow.sh - this
# is a source guard: it asserts the fix is present and correctly shaped
# (counted per shown filesystem, emitted before the df output), which is what
# survives future edits. The renderer half (htmllog honouring an explicit
# linecount override) is exercised behaviourally elsewhere.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
FILE="$ROOT/xymond/xymond_client.c"

[ -f "$FILE" ] || skip "xymond/xymond_client.c absent"
src=$(cat "$FILE")

# The count is incremented once per shown (non-ignored) filesystem - so it
# equals the RRD-file count and excludes IGNORE'd mounts and the df header.
assert_contains "if (!ignored) fscount++;" "$src" \
	"filesystem count must increment per shown (non-ignored) filesystem"

# Each report emits the linecount comment from that count.
n=$(printf '%s\n' "$src" | grep -c 'linecount=%d -->' || true)
[ "$n" -ge 2 ] || fail "both disk and inode reports must emit the linecount hint (found $n)"

# The hint must be emitted BEFORE the df output in each report, so it lands in
# the status body the renderer reads. Check the ordering per report by line.
emit=$(printf '%s\n' "$src" | grep -n 'linecount=%d -->' | cut -d: -f1)
dfout=$(printf '%s\n' "$src" | grep -n 'And the full df output' | cut -d: -f1)
# Pair them up in order: each emit line must precede the next df-output line.
paste <(printf '%s\n' "$emit") <(printf '%s\n' "$dfout") | while read -r e d; do
	[ -n "$e" ] && [ -n "$d" ] && [ "$e" -lt "$d" ] \
		|| { echo "linecount hint (line $e) not before df output (line $d)" >&2; exit 1; }
done || fail "the linecount hint must precede the df output in each report"

pass "disk/inode reports emit an exact linecount hint before the df output"
