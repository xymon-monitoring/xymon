#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/xymond/hostdata-save-throttle.sh
#
# Regression guard: xymond_hostdata must stop saving a host's data once it
# has been saved --recent-count times inside --recent-period.
#
# The limit was kept as twelve save timestamps per host, shifted down on
# each save. The shift stopped at slot 1, so tstamp[1] was never written
# and stayed at its calloc'ed zero for the life of the record; the throttle
# check stopped there, the limit was never reached, and --recent-count did
# nothing at all.
#
# Workers read their messages from stdin (xymond_worker.c), and the save
# times live in per-process state, so the burst has to go through one
# process in a single stream.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
# shellcheck source=tests/lib/xymond-hostdata.sh
. "$(dirname "$0")/../lib/xymond-hostdata.sh"

work=$(mktempdir)

setup_xymond_hostdata "$work"

burst() {  # burst <count> <recent-count> [<recent-period>] -- prints the number of files written
	local n=$1 limit=$2 period=${3:-1} k rc
	set +e
	{
		for k in $(seq 1 "$n"); do
			printf '@@clichg#%d|1|10.0.0.99|realhost|F%d|linux\npayload %d\n@@\n' "$k" "$k" "$k"
		done
	} | run_xymond_hostdata "$work" --recent-period="$period" --recent-count="$limit" \
		>/dev/null 2>>"$work/worker.log"
	rc=$?
	set -e
	[ "$rc" -le 1 ] || fail "xymond_hostdata exited $rc (crash?) during the burst"
	# The host directory only exists once something was saved.
	if [ -d "$work/var/hostdata/realhost" ]; then
		ls "$work/var/hostdata/realhost" | wc -l
	else
		echo 0
	fi
}

# Sanity: saving has to work at all here, or "the limit held" is
# indistinguishable from "nothing was ever written".
saved=$(burst 1 5)
[ "$saved" -eq 1 ] \
	|| { cat "$work/worker.log" >&2; fail "a single clichg was not saved (got $saved) -- the limit check below would be meaningless"; }

# Eight messages, limit five: five saved, the rest dropped.
saved=$(burst 8 5)
[ "$saved" -eq 5 ] \
	|| fail "save throttle ignored: 8 messages with --recent-count=5 wrote $saved files, expected 5"

# The limit is what --recent-count says, not a constant.
saved=$(burst 8 2)
[ "$saved" -eq 2 ] \
	|| fail "--recent-count=2 wrote $saved files, expected 2"

# Below the limit nothing is dropped.
saved=$(burst 3 5)
[ "$saved" -eq 3 ] \
	|| fail "3 messages with --recent-count=5 wrote $saved files, expected 3"

# The history is sized from --recent-count, so limits beyond the once
# fixed twelve slots hold exactly too.
saved=$(burst 14 12)
[ "$saved" -eq 12 ] \
	|| fail "--recent-count=12 wrote $saved files, expected 12"
saved=$(burst 15 13)
[ "$saved" -eq 13 ] \
	|| fail "--recent-count=13 wrote $saved files, expected 13"

# 0 keeps its historical meaning: never save anything.
saved=$(burst 3 0)
[ "$saved" -eq 0 ] \
	|| fail "--recent-count=0 wrote $saved files, expected saving disabled"

# Garbage counts fall back to the default of 5 (with atoi they collapsed
# to 0 and silently turned the module off); negative counts clamp to 0,
# which is what atoi-era negatives effectively did - never save.
saved=$(burst 8 junk)
[ "$saved" -eq 5 ] \
	|| fail "--recent-count=junk wrote $saved files, expected fallback to the default of 5"
saved=$(burst 8 -3)
[ "$saved" -eq 0 ] \
	|| fail "--recent-count=-3 wrote $saved files, expected clamp to 0 (never save, as with atoi)"

# An absurd count is capped, not crashed on: the per-host history is
# allocated from this value.
saved=$(burst 8 2000000000)
[ "$saved" -eq 8 ] \
	|| fail "--recent-count=2000000000 wrote $saved files, expected all 8 (capped limit far above burst)"

# --recent-period=0 is the explicit throttle-off value: the cutoff lands
# at "now" and every message in the burst is saved.
saved=$(burst 8 5 0)
[ "$saved" -eq 8 ] \
	|| fail "--recent-period=0 wrote $saved files, expected all 8 (throttle disabled)"

# Garbage periods fall back to the 60-minute default, inside which the
# burst is still throttled; negative periods clamp to 0 (throttle off),
# which is what atoi-era negatives effectively did - a future cutoff
# counts no saves.
saved=$(burst 8 5 junk)
[ "$saved" -eq 5 ] \
	|| fail "--recent-period=junk wrote $saved files, expected fallback and a throttled burst of 5"
saved=$(burst 8 5 -1)
[ "$saved" -eq 8 ] \
	|| fail "--recent-period=-1 wrote $saved files, expected clamp to 0 (throttle off, as with atoi)"

# A failed save must not consume a throttle slot: five messages whose
# test name overflows PATH_MAX are dropped at the filename check, and
# the five good messages that follow in the same stream must all be
# saved. With the slot charged up front, the failures would burn the
# whole budget and the good data would be dropped.
longname=$(printf 'X%.0s' $(seq 1 5000))
{
	for k in $(seq 1 5); do
		printf '@@clichg#%d|1|10.0.0.99|realhost|%s|linux\npayload %d\n@@\n' "$k" "$longname" "$k"
	done
	for k in $(seq 1 5); do
		printf '@@clichg#%d|1|10.0.0.99|realhost|F%d|linux\npayload %d\n@@\n' "$((k+5))" "$k" "$k"
	done
} | run_xymond_hostdata "$work" --recent-period=1 --recent-count=5 \
	>/dev/null 2>>"$work/worker.log" || true
saved=$(ls "$work/var/hostdata/realhost" 2>/dev/null | wc -l)
[ "$saved" -eq 5 ] \
	|| fail "failed saves burned throttle slots: $saved good files written, expected 5"

pass "xymond_hostdata stops saving a host once it has been saved --recent-count times inside --recent-period"
