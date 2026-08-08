#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin XYMOND_HISTORY xymond/xymond_history

work=$(mktempdir)
mkdir -p "$work/hist"

run_history() {
	set +e
	output=$(env \
		XYMONSERVERLOGS="$work" \
		XYMONALLHISTLOG=TRUE \
		XYMONHOSTHISTLOG=FALSE \
		SAVESTATUSLOG=FALSE \
		"$XYMOND_HISTORY" \
		--histdir="$work/hist" \
		--pidfile="$work/xymond_history.pid" \
		"$@" </dev/null 2>&1)
	status=$?
	set -e
}

run_history --minimum-free=junk
assert_equal 0 "$status" "invalid minimum-free must fall back without aborting"
assert_contains "is invalid (must be 0 through 100), using default 5" "$output" \
	"invalid minimum-free must warn"

run_history --minimum-free=-1
assert_equal 0 "$status" "negative minimum-free must fall back without aborting"
assert_contains "is invalid (must be 0 through 100), using default 5" "$output" \
	"negative minimum-free must warn"

run_history --minimum-free=101
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

run_history --definitely-invalid
assert_equal 0 "$status" "unknown option must be ignored"
assert_contains "Unknown option '--definitely-invalid' - ignored" "$output" \
	"ignored unknown option must identify itself"

run_history --debugging
assert_equal 0 "$status" "an option extending --debug must be ignored"
assert_contains "Unknown option '--debugging' - ignored" "$output" \
	"--debug must require an exact match and report the ignored option"

run_history --histdir=
[ "$status" -ne 0 ] || fail "empty history directory must prevent startup"
assert_contains "No history directory given, aborting" "$output" \
	"empty history directory must explain the failure"

run_history --histlogdir=
[ "$status" -ne 0 ] || fail "empty history-log directory must prevent startup"
assert_contains "History-log directory cannot be empty" "$output" \
	"empty history-log directory must explain the failure"

run_history --pidfile=
[ "$status" -ne 0 ] || fail "empty pidfile must prevent startup"
assert_contains "Pidfile path cannot be empty" "$output" \
	"empty pidfile must explain the failure"

long_pidfile=$(printf 'x%.0s' {1..5000})
run_history --pidfile="$long_pidfile"
assert_equal 0 "$status" "long pidfile must not overflow fixed storage"
assert_contains "Cannot open PID file" "$output" \
	"an unusable long pidfile must report the filesystem error"

pass "xymond_history validates numeric, path, and unknown options"