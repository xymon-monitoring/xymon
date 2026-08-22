#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/rrd/rrd-recreate-after-drop.sh
#
# Guard for the file-exists cache in do_rrd.c (TBT 214): remembering that an
# RRD is present must not outlive the file.
#
# create_and_update_rrd() stat()s the RRD before every update to decide whether
# to create it. Caching that answer per update-cache item removes a syscall per
# value, but the flag then has to be cleared whenever the file can have gone --
# and the paths that make it go are exactly the ones that flush without going
# through the ordinary update: a drophost deletes the host's tree, a renamehost
# moves it, and rrdcacheflush{all,host} commit an item at an arbitrary time.
#
# Left set, the next update skips the create, rrd_update fails on a file that
# is not there, and flush_cached_updates() has already discarded the cached
# readings -- so the data is lost rather than merely late. That is worse than
# the stat it saves, which is why this is asserted rather than reasoned about.
#
# The scenario is the cheapest one that reaches it: report, drop, report again.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin XYMOND_RRD "xymond/xymond_rrd"

work=$(mktempdir)
ts=$(date +%s)
mkdir -p "$work/tmp" "$work/rrd" "$work/home/etc"
: > "$work/home/etc/analysis.cfg"
: > "$work/hosts.cfg"

status_msg() {  # status_msg <msg-timestamp>
	printf '@@status|%s|127.0.0.1|origin|testhost|disk|%s|green||green|%s|0||0||%s|0|linux|/\n' \
		"$1" "$(($1+1800))" "$ts" "$ts"
	printf 'disk report\n'
	printf '/dev/sda1 1000000 400000 600000 40%% /\n'
	printf '@@\n'
}

# HOSTSCFG and an analysis.cfg of our own: without them the worker connects
# out to a live xymond on 127.0.0.1:1984 for every message and, failing that,
# reads the machine's real configuration instead of this fixture.
run_worker() {  # run_worker <rrddir>
	env XYMONHOME="$work/home" XYMONTMP="$work/tmp" HOSTSCFG="$work/hosts.cfg" \
		"$XYMOND_RRD" --rrddir="$1"
}

rrd="$work/rrd/testhost/disk,root.rrd"

# ---- a report creates the RRD ----------------------------------------------

status_msg "$ts" | run_worker "$work/rrd" >"$work/w1.log" 2>&1 \
	|| { cat "$work/w1.log" >&2; fail "the worker rejected the first report"; }
# fail, not skip: require_bin above already covers "no worker", and this
# branch means the creation path itself is broken - which is the wiring under
# test (tests/README.md: never skip for missing project code).
[ -f "$rrd" ] || { ls -R "$work/rrd" >&2; fail "the first report did not create the RRD"; }

# ---- the file goes away underneath the cache -------------------------------
#
# A drop is the honest way to reach it: it is what deletes a host's RRDs, and
# it flushes the update cache on the way out without any update following.
# Doing it in one worker run is what matters -- the cache lives in the process,
# so a fresh process would start with an empty one and prove nothing. The
# update cache is deliberately left on (no --no-cache): with it off every
# update flushes on its own, the drop finds nothing pending, and the path this
# guards is never taken.

{
	status_msg "$ts"
	printf '@@drophost|%s|127.0.0.1|testhost\n@@\n' $((ts+10))
	status_msg $((ts+20))
} | run_worker "$work/rrd" >"$work/w2.log" 2>&1 \
	|| { cat "$work/w2.log" >&2; fail "the worker exited non-zero over drop-then-report"; }

# ---- the report after the drop must recreate it -----------------------------

assert_file_exists "$rrd" \
	"the report after a drophost did not recreate the RRD -- the cached file-exists flag outlived the file"

# ---- and a deletion nothing told us about ----------------------------------
#
# The case the cached flag really risks: an admin removing one RRD by hand
# after a droptest, as xymond_rrd.c documents, or moverrd.sh/rrdreconcile/a
# restore. Nothing announces it, so the miss can only be noticed when the
# write fails - and the readings must survive that.

before=$(($(wc -c < "$rrd")))		# wc -c, not stat: no per-OS spelling

# Through a fifo, so ONE worker sees the file disappear underneath it. Feeding
# a fresh worker after the deletion tests nothing: it starts with an empty
# flag and takes the ordinary stat-and-create path, which is exactly the path
# that was never broken.
fifo="$work/feed"
mkfifo "$fifo"
run_worker "$work/rrd" <"$fifo" >"$work/w3.log" 2>&1 &
worker=$!
exec 9>"$fifo"

status_msg $((ts+20)) >&9
wait_for() {  # wait_for SECONDS TEST...
	local deadline=$((SECONDS + $1)); shift
	while [ "$SECONDS" -lt "$deadline" ]; do eval "$@" && return 0; sleep 0.2; done
	return 1
}
wait_for 20 '[ -f "$rrd" ]' || { cat "$work/w3.log" >&2; fail "the first reading did not reach the RRD"; }

rm -f "$rrd"
status_msg $((ts+40)) >&9
status_msg $((ts+60)) >&9
printf '@@shutdown|1|x\n@@\n' >&9
exec 9>&-
wait "$worker" || { cat "$work/w3.log" >&2; fail "the worker exited non-zero after an external deletion"; }

assert_file_exists "$rrd" \
	"an RRD deleted outside xymond was never recreated -- the cached file-exists flag outlived the file"

after=$(($(wc -c < "$rrd")))
[ "$after" = "$before" ] \
	|| fail "the recreated RRD is not the shape the first one had ($after vs $before bytes)"

# The file coming back is not the point - the readings surviving is. A build
# that recreates an empty RRD and drops every value would satisfy everything
# above, so read the data back. Needs the rrdtool CLI, which librrd-linked
# builds do not imply, hence the guarded skip of this assertion only.
if command -v rrdtool >/dev/null 2>&1; then
	samples=$(rrdtool fetch "$rrd" AVERAGE -s "$((ts-60))" -e "$((ts+120))" 2>/dev/null \
		| awk 'NF > 1 && $2 != "-nan" && $2 != "nan" && $2 != "-" { n++ } END { print n+0 }')
	[ "${samples:-0}" -gt 0 ] \
		|| fail "the recreated RRD holds no reading: the values buffered when the write failed were discarded"
else
	printf "note: rrdtool CLI absent, RRD contents unverified\\n" >&2
fi
