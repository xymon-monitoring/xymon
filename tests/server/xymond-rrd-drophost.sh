#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/xymond-rrd-drophost.sh
#
# The drophost straggler race: @@drophost forks the host directory
# deletion, but messages for that host already queued in the channel can
# still arrive afterwards and recreate RRD files inside (or after) the
# dying directory - leaving a half-resurrected host behind.
#
# The writer decides "straggler" by message time: a message at or before the
# drop's own timestamp was already on its way, anything later means the name
# is in use again. Every assertion here is therefore driven by the timestamps
# in the messages, not by sleeping - the only sleep is the one that lets the
# forked deletion finish, which is what makes the losing interleaving happen.
#
# The barrier table is process-local, so assertions about it have to be made
# inside one worker lifetime: a second invocation starts with an empty table
# and would accept anything.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin XYMOND_RRD "xymond/xymond_rrd"

work=$(mktempdir)
ts=$(date +%s)

status_msg() {  # status_msg <msg-timestamp> [hostname]
	printf '@@status|%s|127.0.0.1|origin|%s|disk|%s|green||green|%s|0||0||%s|0|linux|/\n' \
		"$1" "${2:-testhost}" $(($1+1800)) "$ts" "$ts"
	printf 'disk report\n'
	printf '/dev/sda1 1000000 400000 600000 40%% /\n'
	printf '@@\n'
}

run_worker() {  # run_worker <rrddir> [extra args...]
	local dir=$1; shift
	env XYMONHOME="$work" XYMONTMP="$work/tmp" \
		"$XYMOND_RRD" --rrddir="$dir" "$@"
}

mkdir -p "$work/tmp"

# ---- the race itself: a message older than the drop must not resurrect it ----
# Its own run: in the combined stream below a legitimate later message
# recreates the directory, so "does it exist at the end" could not tell a held
# barrier from a broken one.
rrd1="$work/rrd1"; mkdir -p "$rrd1"
{
	status_msg "$ts"
	printf '@@drophost|%s|127.0.0.1|testhost\n@@\n' $((ts+10))
	# Let the forked deletion FINISH before the straggler arrives. Without
	# it the child's rm usually runs last and hides the recreation by timing
	# luck - the losing interleaving is the one worth pinning.
	sleep 2
	status_msg $((ts+5))		# generated before the drop: a straggler
} | run_worker "$rrd1" --no-cache 2>/dev/null
sleep 1
[ -e "$rrd1/testhost" ] \
	&& fail "a straggler older than the drop recreated the host directory: $(ls "$rrd1/testhost")"

# ---- isolation and re-add, in one worker lifetime ----------------------------
rrd2="$work/rrd2"; mkdir -p "$rrd2"
{
	status_msg "$ts"
	printf '@@drophost|%s|127.0.0.1|testhost\n@@\n' $((ts+10))
	sleep 2
	status_msg $((ts+5))			# straggler: dropped
	status_msg $((ts+11)) otherhost		# never dropped: must land
	status_msg $((ts+20))			# host is back: must land
} | run_worker "$rrd2" --no-cache 2>/dev/null
sleep 1
[ -d "$rrd2/otherhost" ] \
	|| fail "an unrelated host's updates were dropped while another host was barriered"
[ -d "$rrd2/testhost" ] \
	|| fail "a message newer than the drop was refused - a re-added host stays blackholed"

# ---- rename, then rename back ------------------------------------------------
# The barrier is armed on the OLD name, which is a name that can be reused
# immediately. A window-based barrier discards the returning host's data for
# the length of the window; deciding by message time lets it resume at once.
rrd3="$work/rrd3"; mkdir -p "$rrd3"
{
	status_msg "$ts"
	printf '@@renamehost|%s|127.0.0.1|testhost|newhost\n@@\n' $((ts+10))
	status_msg $((ts+20))			# testhost is a live name again
} | run_worker "$rrd3" --no-cache 2>/dev/null
[ -d "$rrd3/newhost" ]  || fail "rename did not move the host directory"
[ -d "$rrd3/testhost" ] \
	|| fail "the old name stayed barriered after it was legitimately reused"

# ---- a rename must barrier the old name too ----------------------------------
# The stock install runs one xymond_rrd per channel over a shared rrddir
# (tasks.cfg.DIST [rrdstatus] and [rrddata]) and xymond posts the marker to
# both, so the second worker to see it finds the directory already gone. It
# still has its own queue of stragglers for the old name, and they must not
# recreate it. Driving one worker, the equivalent is: rename, then a message
# that predates the rename.
rrd3b="$work/rrd3b"; mkdir -p "$rrd3b"
{
	status_msg "$ts"
	printf '@@renamehost|%s|127.0.0.1|testhost|newhost\n@@\n' $((ts+10))
	status_msg $((ts+5))			# predates the rename: a straggler
} | run_worker "$rrd3b" --no-cache 2>/dev/null
[ -d "$rrd3b/newhost" ] || fail "rename did not move the host directory"
[ -e "$rrd3b/testhost" ] \
	&& fail "a straggler older than the rename recreated the old host directory"

# ---- a rename that FAILS must not blackhole the old host ---------------------
# rename() fails on an occupied destination (and on a cross-device rrddir, and
# on a permission problem). The files never moved, so the old host is still
# live and must keep getting updates.
rrd4="$work/rrd4"; mkdir -p "$rrd4/testhost" "$rrd4/newhost"
printf 'occupied\n' >"$rrd4/newhost/occupied.rrd"
{
	printf '@@renamehost|%s|127.0.0.1|testhost|newhost\n@@\n' $((ts+10))
	status_msg $((ts+20))
} | run_worker "$rrd4" --no-cache 2>/dev/null
[ -d "$rrd4/testhost" ] \
	|| fail "the rename should have failed on an occupied destination, but the source is gone"
[ -n "$(find "$rrd4/testhost" -name '*.rrd' 2>/dev/null)" ] \
	|| fail "a failed rename blackholed the old host: its next update was discarded"

# ---- the drop is barriered under the name that is actually deleted -----------
# The hostname arrives as the admin typed it. dropdirectory() reduces it to a
# basename, so if the barrier and the cache purge keep the unreduced form, a
# straggler for the host that really was deleted walks straight past them.
rrd4b="$work/rrd4b"; mkdir -p "$rrd4b"
{
	status_msg "$ts"
	printf '@@drophost|%s|127.0.0.1|foo/testhost\n@@\n' $((ts+10))
	sleep 2
	status_msg $((ts+5))			# straggler for the deleted name
} | run_worker "$rrd4b" --no-cache 2>/dev/null
sleep 1
[ -e "$rrd4b/testhost" ] \
	&& fail "a slash-bearing drop deleted testhost but barriered another name, so the straggler recreated it"

# ...and the cache purge uses that same name. Needs the cache on: with
# --no-cache there is never a pending value for either name to match.
rrd4c="$work/rrd4c"; mkdir -p "$rrd4c"
{
	status_msg "$ts"
	printf '@@drophost|%s|127.0.0.1|foo/testhost\n@@\n' $((ts+10))
} | run_worker "$rrd4c" --debug >"$work/slashdrop.log" 2>&1
sleep 1
grep -q "discarded 1 entries for host testhost" "$work/slashdrop.log" \
	|| fail "a slash-bearing drop purged the cache under the unreduced name: $(grep -i updcache "$work/slashdrop.log")"

# ---- the cache: flush on rename, discard on drop -----------------------------
# Both need the cache ON. With --no-cache every reading is written immediately
# and valcount is always 0, so rrdcache_drop_host() has nothing to act on and
# neither branch is exercised at all.
rrd5="$work/rrd5"; mkdir -p "$rrd5"
{
	status_msg "$ts"
	printf '@@renamehost|%s|127.0.0.1|testhost|newhost\n@@\n' $((ts+10))
} | run_worker "$rrd5" --debug >"$work/rename.log" 2>&1
[ -d "$rrd5/newhost" ] || fail "rename did not move the host directory (cached run)"
grep -q "flushed and dropped 1 entries for host testhost" "$work/rename.log" \
	|| fail "pending update not flushed before the rename: $(grep -i updcache "$work/rename.log")"

rrd6="$work/rrd6"; mkdir -p "$rrd6"
{
	status_msg "$ts"
	printf '@@drophost|%s|127.0.0.1|testhost\n@@\n' $((ts+10))
} | run_worker "$rrd6" --debug >"$work/drop.log" 2>&1
sleep 1
grep -q "discarded 1 entries for host testhost" "$work/drop.log" \
	|| fail "the dropped host's cached update was not discarded: $(grep -i updcache "$work/drop.log")"

pass "drophost discards stragglers by message time, leaves other hosts alone, lets a reused name resume, and survives a failed rename"
