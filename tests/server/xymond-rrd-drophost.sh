#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/xymond-rrd-drophost.sh
#
# The drophost straggler race: @@drophost forks the host directory
# deletion, but messages for that host already queued in the channel can
# still arrive afterwards and recreate RRD files inside (or after) the
# dying directory - leaving a half-resurrected host behind. The writer
# now keeps a drop barrier that discards messages for a recently dropped
# host and purges its cached updates before the deletion starts.
#
# The test forces the losing interleaving: the deletion completes, THEN
# the straggler arrives - without the barrier the recreated directory
# has nothing left to clean it up.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin XYMOND_RRD "xymond/xymond_rrd"

work=$(mktempdir)
mkdir -p "$work/rrd" "$work/tmp"
ts=$(date +%s)

status_msg() {  # status_msg <statusts> -- one disk status for testhost
	printf '@@status|%s|127.0.0.1|origin|testhost|disk|%s|green||green|%s|0||0||%s|0|linux|/\n' \
		"$1" $(($1+1800)) "$ts" "$ts"
	printf 'disk report\n'
	printf '/dev/sda1 1000000 400000 600000 40%% /\n'
	printf '@@\n'
}

{
	status_msg "$ts"
	printf '@@drophost|%s|127.0.0.1|testhost\n@@\n' "$ts"
	# Give the forked deletion time to FINISH before the straggler
	# arrives (without the delay the child's rm usually runs last and
	# hides the recreation by timing luck).
	sleep 2
	status_msg $((ts+1))
} | env XYMONHOME="$work" XYMONTMP="$work/tmp" \
	"$XYMOND_RRD" --rrddir="$work/rrd" --no-cache 2>/dev/null
sleep 1		# any still-running forked deletion
[ -e "$work/rrd/testhost" ] \
	&& fail "straggler recreated the dropped host directory: $(ls "$work/rrd/testhost")"

# A host dropped longer ago than the barrier window resumes normally -
# verified indirectly: a FRESH host (never dropped) is unaffected.
{
	printf '@@status|%s|127.0.0.1|origin|otherhost|disk|%s|green||green|%s|0||0||%s|0|linux|/\n' \
		$((ts+2)) $((ts+1802)) "$ts" "$ts"
	printf 'disk report\n'
	printf '/dev/sda1 1000000 400000 600000 40%% /\n'
	printf '@@\n'
} | env XYMONHOME="$work" XYMONTMP="$work/tmp" \
	"$XYMOND_RRD" --rrddir="$work/rrd" --no-cache 2>/dev/null
[ -d "$work/rrd/otherhost" ] || fail "unrelated host's updates must not be barriered"

# renamehost: pending CACHED updates must flush into the old-named files
# before the rename moves them (rrdcacheflushhost cannot do this: it
# expects "/host"-shaped keys and rate-limits; the old call was a no-op).
rm -rf "$work/rrd"; mkdir -p "$work/rrd" "$work/tmp"
{
	status_msg "$ts"
	printf '@@renamehost|%s|127.0.0.1|testhost|newhost\n@@\n' "$ts"
} | env XYMONHOME="$work" XYMONTMP="$work/tmp" \
	"$XYMOND_RRD" --rrddir="$work/rrd" --debug >"$work/dbg.log" 2>&1
[ -d "$work/rrd/newhost" ] || fail "rename did not move the host directory"
[ -e "$work/rrd/testhost" ] && fail "old directory survived the rename"
grep -q "flushed and dropped 1 entries for host testhost" "$work/dbg.log" \
	|| fail "pending update not flushed before the rename: $(grep -i updcache "$work/dbg.log")"

echo "OK $(basename "$0")"
