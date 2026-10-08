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
#
# The last case checks that the report recreating a file deleted outside
# xymond is written, whatever the worker had cached for it.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin XYMOND_RRD "xymond/xymond_rrd"

ROOT=$(find_root)
require_c_buildenv "$ROOT"
[ -f "$ROOT/Makefile" ] || skip "tree not configured (no Makefile)"

work=$(mktempdir)

# The RRD's contents are read through librrd, with the harness
# inode-unix-columns.sh builds: the rrdtool command is a separate package,
# and a check gated on it would quietly check less where it is missing.
buildflags_output=$("$XYMON_MAKE" -s -C "$ROOT" -f Makefile -f - rrd-test-flags <<'EOF'
.PHONY: rrd-test-flags
rrd-test-flags:
	@printf '%s\n' 'ldflags=$(LDFLAGS)' 'rpathopt=$(RPATHOPT)' \
		'rrddef=$(RRDDEF)' 'rrdincdir=$(RRDINCDIR)' 'rrdlibs=$(RRDLIBS)'
EOF
) || fail "cannot read configured RRD build flags"
buildflags=()
while IFS= read -r line; do buildflags+=("$line"); done <<< "$buildflags_output"
[ "${#buildflags[@]}" -eq 5 ] || fail "configured RRD build flags are incomplete"
ldflags=${buildflags[0]#ldflags=}
rpathopt=${buildflags[1]#rpathopt=}
rrddef=${buildflags[2]#rrddef=}
rrdincdir=${buildflags[3]#rrdincdir=}
rrdlibs=${buildflags[4]#rrdlibs=}
[ -n "$rrdlibs" ] || rrdlibs="-lrrd"
# shellcheck disable=SC2086  # configured flags are word lists by design
"$CC" $ldflags -iquote "$ROOT/include" $rrddef $rrdincdir \
	-o "$work/rrd-lastupdate" $rpathopt \
	"$ROOT/tests/rrd/inode-unix-columns-harness.c" $rrdlibs \
	2>"$work/cc.log" || { cat "$work/cc.log" >&2; fail "librrd last-update harness does not compile"; }

ts=$(date +%s)
mkdir -p "$work/tmp" "$work/rrd" "$work/home/etc"
: > "$work/home/etc/analysis.cfg"
: > "$work/hosts.cfg"

# The sequence number matters when several readings go to one worker: without
# a distinct one each, everything after the first is dropped as a duplicate
# update (do_rrd.c logs it under --debug and nowhere else), so a test that
# feeds a live worker would silently exercise one message.
msgseq=0
status_msg() {  # status_msg <msg-timestamp> [percent-used, default 40]
	local pct=${2:-40}
	msgseq=$((msgseq + 1))
	printf '@@status#%s|%s|127.0.0.1|origin|testhost|disk|%s|green||green|%s|0||0||%s|0|linux|/\n' \
		"$msgseq" "$1" "$(($1+1800))" "$ts" "$ts"
	printf 'disk report\n'
	printf '/dev/sda1 1000000 %s %s %s%% /\n' $((pct * 10000)) $(((100 - pct) * 10000)) "$pct"
	printf '@@\n'
}

# HOSTSCFG and an analysis.cfg of our own: without them the worker connects
# out to a live xymond on 127.0.0.1:1984 for every message and, failing that,
# reads the machine's real configuration instead of this fixture.
run_worker() {  # run_worker <rrddir>
	env XYMONHOME="$work/home" XYMONTMP="$work/tmp" XYMONRUNDIR="$work/tmp" \
		HOSTSCFG="$work/hosts.cfg" "$XYMOND_RRD" --rrddir="$1"
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
# write fails - and the next report must recreate the file.

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

wait_for() {  # wait_for SECONDS TEST...
	local deadline=$((SECONDS + $1)); shift
	while [ "$SECONDS" -lt "$deadline" ]; do eval "$@" && return 0; sleep 0.2; done
	return 1
}

# Deleted first, so waiting for it to come back proves *this* worker created
# it and holds the cached "it exists" answer. Waiting on the file the previous
# case left behind returns instantly and proves nothing.
rm -f "$rrd"
status_msg $((ts+20)) >&9
wait_for 20 '[ -f "$rrd" ]' \
	|| { cat "$work/w3.log" >&2; fail "the first reading did not reach this worker"; }

# Now the flag is stale. What follows has to reach a flush - which is where the
# missing file is noticed - and then an update that recreates: CACHESZ is 12,
# so a couple of readings would sit in the cache until shutdown and never be
# written back.
rm -f "$rrd"
for i in $(seq 1 16); do status_msg $((ts + i*300)) >&9; done
printf '@@shutdown|1|x\n@@\n' >&9
exec 9>&-
wait "$worker" || { cat "$work/w3.log" >&2; fail "the worker exited non-zero after an external deletion"; }

assert_file_exists "$rrd" \
	"an RRD deleted outside xymond was never recreated -- the cached file-exists flag outlived the file"

after=$(($(wc -c < "$rrd")))
[ "$after" = "$before" ] \
	|| fail "the recreated RRD is not the shape the first one had ($after vs $before bytes)"

# The file coming back is not enough: a build that recreates an empty RRD
# would satisfy everything above. So read the last update back: it must hold
# the 40% of the report sent after the drop.
last=$("$work/rrd-lastupdate" "$rrd" 2>&1) \
	|| fail "the recreated RRD cannot be read: $last"
values=$(sed -n '2s/^[0-9]*://p' <<<"$last")
pct=$(awk '{print $1}' <<<"$values")
[ "$pct" = 40 ] \
	|| fail "the recreated RRD does not hold the report sent after the drop (last update: '$values')"

# ---- the report that recreates the file is written --------------------------
#
# What the worker had cached when the RRD vanished cannot be written: the RRD
# recreated at the next update starts at "now", and those readings are older.
# They are discarded at the failed write. The report that recreates the file
# must not go down with them, which it does if they are kept and flushed
# together with it: RRDtool refuses the first, and the whole update with it.
#
# The readings carry past timestamps, as a real cache holds them. The case
# above sends future ones, which any recreated RRD accepts.

rrd5="$work/rrd5/testhost/disk,root.rrd"
mkdir -p "$work/rrd5"
fifo5="$work/feed5"
mkfifo "$fifo5"
run_worker "$work/rrd5" <"$fifo5" >"$work/w5.log" 2>&1 &
worker=$!
exec 9>"$fifo5"

# Call 1 creates the RRD and caches its reading.
status_msg $((ts - 3600)) >&9
wait_for 20 '[ -f "$rrd5" ]' \
	|| { cat "$work/w5.log" >&2; fail "the past-readings case's first reading did not reach its worker"; }

# Calls 2-11 are cached against the stale flag. Call 12 is the one in CACHESZ
# forced through: its write fails on the missing file. Call 13 recreates the
# file and is the reading checked.
rm -f "$rrd5"
for i in $(seq 1 11); do status_msg $((ts - 3600 + i*300)) >&9; done
status_msg "$ts" 55 >&9
printf '@@shutdown|1|x\n@@\n' >&9
exec 9>&-
wait "$worker" || { cat "$work/w5.log" >&2; fail "the worker exited non-zero over the past-readings case"; }

assert_file_exists "$rrd5" "an RRD deleted outside xymond under past readings was never recreated"
last=$("$work/rrd-lastupdate" "$rrd5" 2>&1) \
	|| fail "the RRD recreated under past readings cannot be read: $last"
lastline=$(sed -n '2p' <<<"$last")
[ "${lastline%%:*}" = "$ts" ] && [ "$(awk '{print $1}' <<<"${lastline#*:}")" = 55 ] \
	|| fail "the report that recreated an RRD deleted outside xymond was not written: readings cached before the deletion were flushed with it and refused (last update: '$lastline', expected $ts: 55)"

pass "an RRD dropped, or deleted outside xymond, is recreated, and takes the next report, by a worker that keeps its file-exists cache"
