#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/tasks-periodic-reports.sh
#
# The shipped tasks.cfg carries one task per periodic report - daily, weekly
# and monthly - that runs xymonreports.sh on the schedule the script's own
# header suggests, and each ships DISABLED. Without them nothing generates the
# reports datepage.cgi sends the browser to, and nothing in the configuration
# says that the reports exist.
#
# Checked through xymonlaunch --dump, which prints each task as xymonlaunch
# parsed it: a task that is present, disabled, runs the right command and has
# the right CRONDATE parsed that way, not merely appears in the text.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin XYMONLAUNCH "common/xymonlaunch"
require_cfg TASKSCFG "xymond/etcfiles/tasks.cfg"

work=$(mktempdir)
"$XYMONLAUNCH" --config="$TASKSCFG" --dump >"$work/dump" 2>"$work/err" \
	|| fail "xymonlaunch --dump could not read $TASKSCFG: $(cat "$work/err")"

# section NAME -- the lines xymonlaunch dumped for task NAME, leading tab removed
section() {
	awk -v s="[$1]" '$0 == s { on = 1; next } on && /^$/ { exit } on { sub(/^\t/, ""); print }' "$work/dump"
}

for spec in "daily:10 0 * * *" "weekly:10 1 * * 1" "monthly:10 2 1 * *"; do
	type=${spec%%:*}
	cron=${spec#*:}
	task="periodic-$type"
	lines=$(section "$task")
	[ -n "$lines" ] || fail "tasks.cfg has no [$task] task, so nothing generates the $type reports"
	grep -Fqx "DISABLED" <<<"$lines" \
		|| fail "[$task] does not ship DISABLED, so an upgrade would start writing $type reports unasked"
	grep -Fqx "CMD xymonreports.sh $type" <<<"$lines" \
		|| fail "[$task] does not run xymonreports.sh $type: $lines"
	grep -Fqx "CRONDATE $cron" <<<"$lines" \
		|| fail "[$task] is not scheduled at '$cron': $lines"
done

pass "tasks.cfg ships daily, weekly and monthly periodic-report tasks, disabled, each running xymonreports.sh on its schedule"
