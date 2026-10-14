#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/xymond/hostdata-recent-window.sh
#
# Regression guard: xymond_hostdata must save client data even when the
# machine's uptime is below --recent-period.
#
# A first-seen host has calloc'ed (zero) save timestamps, and gettimer() is
# CLOCK_MONOTONIC (seconds since boot), so while uptime is below the window
# "now - recentperiod" is negative, every empty slot counts as a recent
# save, and the data is dropped unlogged.
#
# The test cannot reboot the host, but the comparison only depends on
# "now - recentperiod", so a window larger than the current uptime
# reproduces the condition exactly.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
# shellcheck source=tests/lib/xymond-hostdata.sh
. "$(dirname "$0")/../lib/xymond-hostdata.sh"

work=$(mktempdir)

setup_xymond_hostdata "$work"

save() {  # save <recent-period-minutes> -- returns 0 if the data was written
	printf '@@clichg#1|1|10.0.0.99|realhost|SAVED|linux\nclient payload\n@@\n' \
	| run_xymond_hostdata "$work" --recent-period="$1" >/dev/null 2>&1
	[ -f "$work/var/hostdata/realhost/SAVED" ]
}

# A window no uptime can outgrow (~19 years; 60*10000000 still fits the
# signed int used by option parsing) stands in for a freshly booted host.
save 10000000 \
	|| fail "client data dropped when the save window exceeds uptime -- a rebooted server loses hostdata until it has been up for --recent-period"

# The ordinary case must keep working: a window well under this machine's
# uptime, and the explicit throttle-off value.
save 60 || fail "client data not saved with the default-sized window"
save 0  || fail "client data not saved with --recent-period=0 (throttle disabled)"

pass "xymond_hostdata saves client data even when uptime is below --recent-period"
