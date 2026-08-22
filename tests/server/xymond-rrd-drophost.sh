#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/xymond-rrd-drophost.sh
#
# @@drophost and @@renamehost in the RRD writer.
#
#   - the deletion is synchronous, so a message that follows the drop in the
#     channel cannot land inside it and leave orphaned RRD files behind;
#   - pending cached updates are discarded before the files are deleted and
#     flushed before they are renamed (without the flush they are lost on
#     every rename: the cache is keyed on the old path);
#   - both names are confined to a single path component, so a "." or ".."
#     cannot turn a recursive delete loose on the RRD tree or its parent.
#
# Message ordering here matches the real channel: xymond stamps metadata[1] at
# post time and delivers in that order, so a status that follows a drop marker
# is always stamped at or after it. Nothing here depends on a stream the
# channel cannot produce.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin XYMOND_RRD "xymond/xymond_rrd"

work=$(mktempdir)
ts=$(date +%s)
mkdir -p "$work/tmp"

status_msg() {  # status_msg <msg-timestamp> [hostname]
	printf '@@status|%s|127.0.0.1|origin|%s|disk|%s|green||green|%s|0||0||%s|0|linux|/\n' \
		"$1" "${2:-testhost}" $(($1+1800)) "$ts" "$ts"
	printf 'disk report\n'
	printf '/dev/sda1 1000000 400000 600000 40%% /\n'
	printf '@@\n'
}

run_worker() {  # run_worker <rrddir> [extra args...]
	local dir=$1; shift
	# XYMONRUNDIR is where xymond_rrd binds its cache-control socket. Its
	# fallback is XYMONLOGDIR, which resolves to the build-time install path
	# and is not writable from a test tree - the bind then fails and the
	# worker exits non-zero before it reads a single message.
	env XYMONHOME="$work" XYMONTMP="$work/tmp" XYMONRUNDIR="$work/tmp" \
		"$XYMOND_RRD" --rrddir="$dir" "$@"
}

# ---- the drop removes the host's directory ---------------------------------
# The deletion used to be forked, so a status read while the child was still
# emptying the directory recreated it and the child's final rmdir() failed,
# leaving orphaned RRD files for a host that no longer exists. A synchronous
# delete completes before the next message is read.
rrd1="$work/rrd1"; mkdir -p "$rrd1"
{
	status_msg "$ts"
	printf '@@drophost|%s|127.0.0.1|testhost\n@@\n' $((ts+10))
} | run_worker "$rrd1" --no-cache 2>/dev/null
[ -e "$rrd1/testhost" ] \
	&& fail "the host directory survived the drop: $(ls "$rrd1/testhost")"

# ---- the deletion must be finished when the worker is ------------------------
# The assertion above passes either way: with a directory this small the forked
# child wins the race long before the check runs, so it does not notice a
# reverted barrier. Give the child real work instead -- enough files that a
# background delete is still running when the parent exits -- and the two
# behaviours separate: synchronously the directory is gone the moment the
# worker returns, forked it is still being emptied.
#
# Timing-based, so sized with margin rather than tuned: ~50k files take the
# order of 100ms to remove, against the microseconds the parent needs to finish
# the drop and exit. A slower machine widens the gap rather than narrowing it.
rrd3="$work/rrd3"; mkdir -p "$rrd3/testhost"
( cd "$rrd3/testhost" && seq 1 50000 | sed 's/$/.rrd/' | xargs -n 500 touch )
{
	status_msg "$ts"
	printf '@@drophost|%s|127.0.0.1|testhost\n@@\n' $((ts+10))
} | run_worker "$rrd3" --no-cache 2>/dev/null
if [ -e "$rrd3/testhost" ]; then
	# Still there: with the barrier the delete finishes before the worker
	# returns, so anything left means it was handed to a forked child again.
	left=$(find "$rrd3/testhost" -type f 2>/dev/null | sed -n 1p || true)
	fail "the drop was still in progress when the worker exited (${left:-directory still present}) -- the deletion is forked again"
fi

# ---- a drop deletes nothing but the host it names ---------------------------
# basename("..") is "..", so appending it to rrddir named the parent of the
# whole RRD tree and handed it to a recursive delete. safe_basename() refuses
# these outright rather than reducing them.
for name in "." ".." "/" "foo/testhost"; do
	rrdx="$work/rrdx"; rm -rf "$rrdx"; mkdir -p "$rrdx/testhost" "$rrdx/otherhost"
	printf 'canary\n'         >"$rrdx/otherhost/keep.rrd"
	printf 'canary\n'         >"$rrdx/testhost/keep.rrd"
	printf 'sibling canary\n' >"$work/SIBLING.txt"
	printf '@@drophost|%s|127.0.0.1|%s\n@@\n' $((ts+10)) "$name" \
		| run_worker "$rrdx" --no-cache 2>/dev/null
	[ -f "$rrdx/otherhost/keep.rrd" ] \
		|| fail "drop '$name' deleted an unrelated host's RRDs"
	[ -f "$rrdx/testhost/keep.rrd" ] \
		|| fail "drop '$name' deleted testhost, which it did not name"
	[ -f "$work/SIBLING.txt" ] \
		|| fail "drop '$name' escaped the RRD directory entirely"
done

# ---- rename moves the directory ---------------------------------------------
rrd3="$work/rrd3"; mkdir -p "$rrd3"
{
	status_msg "$ts"
	printf '@@renamehost|%s|127.0.0.1|testhost|newhost\n@@\n' $((ts+10))
} | run_worker "$rrd3" --no-cache 2>/dev/null
[ -d "$rrd3/newhost" ]  || fail "rename did not move the host directory"
[ -e "$rrd3/testhost" ] && fail "old directory survived the rename"

# ---- ...and confines both of its names --------------------------------------
for pair in "..|newhost" "testhost|.." "foo/testhost|newhost" "testhost|foo/newhost"; do
	rrdy="$work/rrdy"; rm -rf "$rrdy"; mkdir -p "$rrdy/testhost"
	printf 'canary\n'         >"$rrdy/testhost/keep.rrd"
	printf 'sibling canary\n' >"$work/SIBLING.txt"
	printf '@@renamehost|%s|127.0.0.1|%s|%s\n@@\n' $((ts+10)) "${pair%|*}" "${pair#*|}" \
		| run_worker "$rrdy" --no-cache 2>/dev/null
	[ -f "$rrdy/testhost/keep.rrd" ] \
		|| fail "rename '${pair%|*}' -> '${pair#*|}' moved a directory it did not name"
	[ -f "$work/SIBLING.txt" ] \
		|| fail "rename '${pair%|*}' -> '${pair#*|}' escaped the RRD directory"
done

# ---- the cache: flushed before a rename, discarded before a delete ----------
# Both need the cache on. With --no-cache every reading is written immediately
# and valcount stays 0, so rrdcache_drop_host() has nothing to act on and
# neither branch runs at all.
rrd4="$work/rrd4"; mkdir -p "$rrd4"
{
	status_msg "$ts"
	printf '@@renamehost|%s|127.0.0.1|testhost|newhost\n@@\n' $((ts+10))
} | run_worker "$rrd4" --debug >"$work/rename.log" 2>&1
[ -d "$rrd4/newhost" ] || fail "rename did not move the host directory (cached run)"
# The reading must be flushed BEFORE the rename, not swept up by the worker's
# shutdown flush - by then the old path is gone and the write fails. Ordering
# against the shutdown line is what separates the two: the "flushed and dropped
# N entries" summary is printed from the match count whether or not anything
# was written, and flush_cached_updates() logs the same line in both cases.
fl=$(grep -n "Flushing '/testhost/" "$work/rename.log" | head -1 | cut -d: -f1)
sd=$(grep -n "Shutting down, flushing cached updates" "$work/rename.log" | head -1 | cut -d: -f1)
[ -n "$fl" ] || fail "the pending update was never flushed: $(grep -iE 'updcache|Flushing' "$work/rename.log")"
[ -n "$sd" ] || fail "no shutdown-flush line to order against: $(cat "$work/rename.log")"
[ "$fl" -lt "$sd" ] \
	|| fail "the pending update was flushed at shutdown, after the rename moved its file, not before"
[ -n "$(find "$rrd4/newhost" -name '*.rrd' 2>/dev/null)" ] \
	|| fail "the flushed reading did not land in the renamed directory"

rrd5="$work/rrd5"; mkdir -p "$rrd5"
{
	status_msg "$ts"
	printf '@@drophost|%s|127.0.0.1|testhost\n@@\n' $((ts+10))
} | run_worker "$rrd5" --debug >"$work/drop.log" 2>&1
grep -q "discarded 1 entries for host testhost" "$work/drop.log" \
	|| fail "the dropped host's cached update was not discarded: $(grep -i updcache "$work/drop.log")"
[ -e "$rrd5/testhost" ] && fail "the dropped host's directory survived (cached run)"

pass "drophost deletes only the host it names and cannot be raced; renamehost flushes the cache and confines both names"
