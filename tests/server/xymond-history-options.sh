#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin XYMOND_HISTORY xymond/xymond_history

work=$(mktempdir)
mkdir -p "$work/hist" "$work/histlogs"
input=/dev/null
allhist=TRUE
hosthist=FALSE
statuslogs=FALSE
pidarg="--pidfile=$work/xymond_history.pid"

printf '%s\n' \
	'@@stachg#1|1586206051.00000|127.0.0.1|xymond|test.host|disk|1586206351|red|green|1586205451|0||0|0|' \
	'body' '@@' > "$work/message"

run_history() {
	set +e
	output=$(env \
		XYMONSERVERLOGS="$work" \
		XYMONHISTLOGS="$work/histlogs" \
		XYMONALLHISTLOG="$allhist" \
		XYMONHOSTHISTLOG="$hosthist" \
		SAVESTATUSLOG="$statuslogs" \
		"$XYMOND_HISTORY" \
		--histdir="$work/hist" \
		${pidarg:+"$pidarg"} \
		"$@" <"$input" 2>&1)
	status=$?
	set -e
}

input="$work/message"
statuslogs=TRUE
run_history --minimum-free=junk
assert_equal 0 "$status" "invalid minimum-free must fall back without aborting"
assert_contains "is not a plain integer, using default 5" "$output" \
	"invalid minimum-free must warn"

run_history --minimum-free=10%
assert_equal 0 "$status" "minimum-free with a unit must fall back without aborting"
assert_contains "--minimum-free=10% is not a plain integer, using default 5" "$output" \
	"minimum-free with a unit must be rejected as a non-integer"

run_history --minimum-free=-5
assert_equal 0 "$status" "negative minimum-free must retain compatibility"
assert_contains "is below 0, using 0" "$output" \
	"negative minimum-free must clamp to its historical disabled value"
assert_not_contains "less than" "$output" \
	"negative minimum-free must keep the free-space check disabled"

run_history --minimum-free=200
assert_equal 0 "$status" "minimum-free above 100 must be capped without aborting"
assert_contains "is too large, capping at 100" "$output" \
	"minimum-free above 100 must warn"

run_history --minimum-free=99999999999999999999999999999999999999
assert_equal 0 "$status" "overflowing minimum-free must be capped without aborting"
assert_contains "is too large, capping at 100" "$output" \
	"overflowing minimum-free must warn"

for boundary in 0 100; do
	run_history --minimum-free="$boundary"
	assert_equal 0 "$status" "minimum-free=$boundary must be accepted"
	assert_not_contains "is invalid" "$output" "valid boundary must not warn"
	assert_not_contains "capping" "$output" "valid boundary must not be capped"
done
run_history --minimum-free=0
assert_not_contains "less than" "$output" \
	"minimum-free=0 must disable the runtime free-space check"
input=/dev/null
statuslogs=FALSE

run_history --definitely-invalid
assert_equal 0 "$status" "unknown option must be ignored"
assert_contains "Unknown option '--definitely-invalid' - ignored" "$output" \
	"ignored unknown option must identify itself"

for debugopt in --debug --debug=1; do
	run_history "$debugopt"
	assert_equal 0 "$status" "$debugopt must retain debug compatibility"
	assert_not_contains "Unknown option" "$output" \
		"$debugopt must still enable debug mode"
	assert_contains "get_xymond_message:" "$output" \
		"$debugopt must produce debug output"
done

allhist=FALSE
run_history --histdir=
assert_equal 0 "$status" "empty history directory must retain upgrade compatibility"

statuslogs=TRUE
input="$work/message"
rm -f "$work/hist/test,host.disk"
run_history --histlogdir=
assert_equal 0 "$status" "empty history-log directory must not prevent startup"
assert_file_exists "$work/hist/test,host.disk" \
	"empty history-log directory must not prevent message processing"
input=/dev/null

statuslogs=FALSE
run_history --pidfile=
assert_equal 0 "$status" "empty pidfile must not prevent history processing"
assert_contains "Cannot open PID file ''" "$output" \
	"empty pidfile must report the unusable path"

pidarg=
history_fifo="$work/history-input"
mkfifo "$history_fifo"
env \
	XYMONSERVERLOGS="$work" \
	XYMONHISTLOGS="$work/histlogs" \
	XYMONALLHISTLOG=FALSE \
	XYMONHOSTHISTLOG=FALSE \
	SAVESTATUSLOG=FALSE \
	"$XYMOND_HISTORY" \
	--histdir="$work/hist" \
	<"$history_fifo" >"$work/default-pid.log" 2>&1 &
history_worker=$!
register_cleanup "kill $history_worker 2>/dev/null"
exec 3>"$history_fifo"
for _ in {1..1000}; do
	[ -s "$work/xymond_history.pid" ] && break
done
assert_file_exists "$work/xymond_history.pid" "default pidfile"
default_pid=$(cat "$work/xymond_history.pid")
assert_match '^[0-9]+$' "$default_pid" "default pidfile must contain a PID"
kill -0 "$default_pid" 2>/dev/null || fail "default pidfile does not identify the running worker"
exec 3>&-
set +e
wait "$history_worker"
status=$?
set -e
assert_equal 0 "$status" "default pidfile path must work"
[ ! -e "$work/xymond_history.pid" ] || \
	fail "default pidfile was not removed on clean shutdown"

pidarg="--pidfile=/dev/full"
run_history
assert_equal 0 "$status" "pidfile write failure must not stop history processing"
assert_contains "Cannot write PID file '/dev/full'" "$output" \
	"pidfile close failure must identify the path"

pidarg="--pidfile=$work/pid-link"
ln -s "$work/hist" "$work/pid-link"
run_history
assert_equal 0 "$status" "pidfile open failure must not stop history processing"
assert_contains "Cannot open PID file '$work/pid-link'" "$output" \
	"pidfile failure must identify the path"
[ -L "$work/pid-link" ] || fail "worker removed a pidfile it never opened"
rm "$work/pid-link"

pidarg="--pidfile=$work/stale.pid"
allhist=TRUE
run_history --histdir="$work/missing"
[ "$status" -ne 0 ] || fail "unopenable allevents path must prevent startup"
[ ! -e "$work/stale.pid" ] || \
	fail "startup validation failure left a stale pidfile"

allhist=TRUE
hosthist=TRUE
long_testname=$(printf 't%.0s' {1..5000})
rm -f "$work/hist/allevents" "$work/hist/short.host"
printf '@@stachg#1|1586206051.00000|127.0.0.1|xymond|short.host|%s|1586206351|red|green|1586205451|0||0|0|\nbody\n@@\n' \
	"$long_testname" > "$work/message"
input="$work/message"
run_history
assert_equal 0 "$status" "long message-derived path must not crash"
assert_contains "History path is too long" "$output" \
	"long message-derived path must be rejected"
[ -s "$work/hist/allevents" ] || \
	fail "rejected status path suppressed the all-events record"
[ -s "$work/hist/short.host" ] || \
	fail "rejected status path suppressed the per-host record"
input=/dev/null
allhist=FALSE
hosthist=FALSE

long_pidfile=$(printf 'x%.0s' {1..5000})
run_history --pidfile="$long_pidfile"
assert_equal 0 "$status" "long pidfile must not overflow fixed storage"
assert_contains "Cannot open PID file" "$output" \
	"an unusable long pidfile must report the filesystem error"

pass "xymond_history validates numeric, path, and unknown options"