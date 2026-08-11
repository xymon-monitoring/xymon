#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/xymond-checkpoint-roundtrip.sh
#
# Drives a real xymond through save and restore of its checkpoint file.
#
# Three things this pins, all of which are invisible to a compile and to any
# source-level check:
#
#   1. The status record keeps the field count every released xymond expects.
#      Its reader errors on a field it does not know and skips the whole line,
#      so one extra field costs an older build the entire board -- statuses,
#      acknowledgements and disables -- the first time someone rolls back.
#      Flap and hold-time state therefore rides in its own ".flapstate."
#      record, which unrecognised-record skipping already swallows.
#
#   2. Both flap colours survive a restart. Left at the calloc default the
#      first update after startup compares a real colour against a fabricated
#      green and records a status change that never happened.
#
#   3. The flapping flag is re-derived from the restored ring, not taken from
#      the file. The file says "flapping when we checkpointed", which after a
#      long stop is not "flapping now".
#
#   4. A no-data gap is carried while it can still bridge a hold-time, and
#      dropped once it cannot. A gap only ends when the recorded color becomes
#      the reporter's own again, which never happens while something overrides
#      it, so without the second half the record is written forever.
#
# Needs a built tree: xymond itself and the xymon client.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)

require_bin XYMOND xymond/xymond
require_bin XYMONCLIENT common/xymon

command -v sed >/dev/null 2>&1 || skip "sed not available"

work=$(mktempdir)

XYMOND_PID=""
stop_xymond() {
	[ -n "$XYMOND_PID" ] || return 0
	kill "$XYMOND_PID" 2>/dev/null || true
	# xymond writes its checkpoint during shutdown; wait for it to finish.
	local i=0
	while kill -0 "$XYMOND_PID" 2>/dev/null && [ "$i" -lt 100 ]; do
		sleep 0.1
		i=$((i+1))
	done
	XYMOND_PID=""
}
register_cleanup stop_xymond

printf 'page test Test\n127.0.0.1 testhost.example.com # conn\n' > "$work/hosts.cfg"

# xymond refuses to start unless XYMONHOME names a real directory, so the
# stock xymonserver.cfg -- which points at the install prefix -- only works on
# a machine that already has Xymon installed. Point it at the test's own
# directory instead, and create what xymond expects to find under it.
mkdir -p "$work/home/etc" "$work/home/tmp" "$work/home/www"
sed -e 's|^XYMONHOME=.*|XYMONHOME="'"$work"'/home"|' \
    -e 's|^XYMONTMP=.*|XYMONTMP="'"$work"'/home/tmp"|' \
	"$ROOT/xymond/etcfiles/xymonserver.cfg" > "$work/xymonserver.cfg" \
	|| skip "no xymonserver.cfg to run against"

# free_port -- a 127.0.0.1 port nothing is listening on. Racy in principle;
# the window is a few milliseconds and only this test uses the port.
free_port() {
	local p tries=0
	while [ "$tries" -lt 50 ]; do
		p=$(( 20000 + (RANDOM % 20000) ))
		"$XYMONCLIENT" "127.0.0.1:$p" "ping" >/dev/null 2>&1 || { printf '%s' "$p"; return 0; }
		tries=$((tries+1))
	done
	return 1
}

# start_xymond [extra args...] -- boot xymond on a fresh port and wait for it
# to answer. Sets PORT.
start_xymond() {
	local i=0
	PORT=$(free_port) || fail "no free port for xymond"
	"$XYMOND" --no-daemon --listen="127.0.0.1:$PORT" \
		--hosts="$work/hosts.cfg" --env="$work/xymonserver.cfg" \
		--pidfile="$work/xymond.pid" --checkpoint-file="$work/chk" \
		--flap-count=2 --flap-seconds=3600 "$@" \
		> "$work/xymond.log" 2>&1 &
	XYMOND_PID=$!

	while [ "$i" -lt 100 ]; do
		"$XYMONCLIENT" "127.0.0.1:$PORT" "ping" >/dev/null 2>&1 && return 0
		kill -0 "$XYMOND_PID" 2>/dev/null || { cat "$work/xymond.log" >&2; fail "xymond exited during startup"; }
		sleep 0.1
		i=$((i+1))
	done
	cat "$work/xymond.log" >&2
	fail "xymond did not answer on 127.0.0.1:$PORT"
}

# The host part of a status message spells dots as commas.
send_status() {
	"$XYMONCLIENT" "127.0.0.1:$PORT" "status testhost,example,com.conn $1 msg-$1" \
		|| fail "cannot send a $1 status to xymond"
}

board() {
	"$XYMONCLIENT" "127.0.0.1:$PORT" "xymondboard fields=$1" 2>/dev/null
}

# conn_field N -- field N (1-based) of the conn row of a board response.
conn_field() {
	board "$1" | awk -F'|' -v col="$2" '$2 == "conn" { print $col }'
}

# ---- save --------------------------------------------------------------------

start_xymond

# Two colour changes with --flap-count=2 puts the test into the flapping state,
# so there is flap state worth checkpointing.
for colour in red green red yellow; do
	send_status "$colour"
	sleep 0.3
done

flapinfo=$(conn_field "hostname,testname,flapinfo" 3)
[ -n "$flapinfo" ] || fail "conn test never reached the board"
case "$flapinfo" in
  1/*) ;;
  *) fail "expected the conn test to be flapping before the checkpoint, got flapinfo=$flapinfo" ;;
esac

stop_xymond
[ -s "$work/chk" ] || fail "xymond wrote no checkpoint file"

# 1. The status record must still carry exactly the field count released
#    versions parse. This is the check that would have caught appending the
#    flap fields to it.
statusfields=$(awk -F'|' '$2 !~ /^\./ { print NF; exit }' "$work/chk")
[ "$statusfields" = "20" ] \
	|| fail "status record has $statusfields fields, expected the released 20 -- an older xymond would reject it and drop the whole board:
$(grep -v '|\.' "$work/chk" | head -1)"

# 2. The flap state went somewhere an older reader skips.
flapline=$(grep '^@@XYMONDCHK-V1|\.flapstate\.|' "$work/chk" | head -1)
[ -n "$flapline" ] \
	|| fail "no .flapstate. record written for a flapping test:
$(cat "$work/chk")"

# Both flap colours must be in it, or there is nothing to restore in step 2.
savedcolours=$(printf '%s' "$flapline" | awk -F'|' '{ print $6 "/" $7 }')
[ "$savedcolours" = "red/yellow" ] \
	|| fail "expected the saved flap colours red/yellow, got $savedcolours"

# ---- restore -----------------------------------------------------------------

start_xymond --restart="$work/chk"

restored=$(conn_field "hostname,testname,flapinfo" 3)
[ -n "$restored" ] \
	|| fail "the status record did not survive the restart -- the whole board was dropped:
$(cat "$work/chk")"

restoredcolours=$(printf '%s' "$restored" | awk -F'/' '{ print $4 "/" $5 }')
[ "$restoredcolours" = "red/yellow" ] \
	|| fail "flap colours came back as $restoredcolours, expected red/yellow (green/green means they were never restored)"

case "$restored" in
  1/*) ;;
  *) fail "a checkpoint inside the flap window should restore as flapping, got flapinfo=$restored" ;;
esac

stop_xymond

# ---- restore from a checkpoint older than the flap window --------------------
#
# Same file, with the change ring backdated well past --flap-seconds. The saved
# flag still says "flapping"; the ring says the test has been quiet for a day,
# and the ring is what counts.

awk -F'|' -v OFS='|' '$2 == ".flapstate." { $NF = "1000000,1000000" } { print }' \
	"$work/chk" > "$work/chk.stale"
grep -q '^@@XYMONDCHK-V1|\.flapstate\.|.*|1000000,1000000$' "$work/chk.stale" \
	|| fail "could not backdate the flap ring; the .flapstate. record format changed"

start_xymond --restart="$work/chk.stale"

stale=$(conn_field "hostname,testname,flapinfo" 3)
[ -n "$stale" ] || fail "status record did not survive the restart from the backdated checkpoint"
case "$stale" in
  0/*) ;;
  *) fail "a checkpoint older than the flap window must not restore as flapping, got flapinfo=$stale" ;;
esac

stop_xymond

# ---- a gap is carried while it can still bridge, and not once it cannot -----
#
# The gap fields exist to let a resumed test carry its hold-time across the
# silence, which holdtime_bridges() refuses past GAPBRIDGE_VALIDITIES report
# validities. Past that the gap decides nothing, so it must not be written --
# otherwise a test whose color xymond permanently overrides (an RRDDS modifier,
# refreshed by xymond_client every client cycle) carries a record in every
# checkpoint from the first override until the modifier stops.
#
# Both halves restore and save without sending a status, so the gap is written
# back exactly as the daemon holds it. The restored validity is defaultvalidity,
# 30 minutes, so the window is two hours.

# write_gap_checkpoint GAPSTART -- conn showing the CLEAR xymond invented, with
# a gap open since GAPSTART carrying a red held from an hour ago.
write_gap_checkpoint() {
	local gapstart=$1 held=$(( $(date +%s) - 3600 ))

	printf '@@XYMONDCHK-V1||testhost.example.com|conn|127.0.0.1|clear||red|%s|%s|%s|0|0|0|0|status testhost,example,com.conn clear invented|||0|0\n' \
		"$held" "$held" "$(( $(date +%s) + 86400 ))" > "$work/chk.gap"
	printf '@@XYMONDCHK-V1|.flapstate.|testhost.example.com|conn|0|none|none|red|%s|%s|0,0,0,0,0\n' \
		"$held" "$gapstart" >> "$work/chk.gap"
}

# saved_gapcolor -- the gap color in the checkpoint xymond just wrote, or
# "none" when it wrote no .flapstate. record at all.
saved_gapcolor() {
	awk -F'|' '$2 == ".flapstate." { print $8; found=1 } END { if (!found) print "none" }' "$work/chk"
}

write_gap_checkpoint "$(( $(date +%s) - 60 ))"
start_xymond --restart="$work/chk.gap"
stop_xymond
[ "$(saved_gapcolor)" = "red" ] \
	|| fail "a gap 60s old was not carried through a restart (got $(saved_gapcolor)); the hold-time it would bridge is lost"

write_gap_checkpoint "$(( $(date +%s) - 86400 ))"
start_xymond --restart="$work/chk.gap"
stop_xymond
[ "$(saved_gapcolor)" = "none" ] \
	|| fail "a day-old gap was written back as $(saved_gapcolor); nothing can bridge it any more, so it is state that never goes away"

pass "checkpoint round-trip keeps the released status record, carries the flap state, re-derives flapping, and drops a gap past its bridge window"
