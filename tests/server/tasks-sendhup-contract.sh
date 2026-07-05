#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/tasks-sendhup-contract.sh
#
# SENDHUP tells xymonlaunch to pass a log-rotation signal on to the task. The
# keyword says nothing about whether the task can act on it -- the launcher
# sends it either way -- so a task named here whose binary does not treat
# SIGHUP as a log switch goes on writing to the rotated file, and whoever put
# the line there has no way to find out.
#
# The shipped configs are the promise, so they are what this checks: a task
# carrying SENDHUP must name a binary that treats SIGHUP as a log switch and
# reads $XYMONLAUNCH_LOGFILENAME, since a launcher-started task gets its log
# through the launcher's LOGFILE and not on its own command line.
#
# A contract over the shipped configuration, not a test of the rotation: that
# needs a live daemon with a server in front of it. It fails the day someone
# adds SENDHUP to a task without the code behind it.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)

configs="$ROOT/xymond/etcfiles/tasks.cfg.DIST $ROOT/client/clientlaunch.cfg.DIST"
checked=0

for cfg in $configs; do
	[ -f "$cfg" ] || fail "shipped config missing: $cfg"

	# Walk the file keeping the current section and its CMD, and report the
	# pair when SENDHUP turns up. awk rather than a shell loop so the section
	# state is kept in one place.
	while read -r task cmd; do
		[ -n "$task" ] || continue
		checked=$((checked + 1))

		# The binary is the first word of CMD, which may carry a $VAR path.
		bin=$(basename "${cmd%% *}")
		# POSIX `! -path`, not GNU `-not` (NetBSD find rejects `-not`); and no
		# `| head` -- under `set -o pipefail`, head closing the pipe while find
		# still walks a large tree raises SIGPIPE (rc=141) on BSD. Capture all
		# matches, keep the first line.
		src=$(find "$ROOT" -name "$bin.c" ! -path '*/tests/*')
		src=${src%%$'\n'*}
		[ -n "$src" ] || fail "$(basename "$cfg"): [$task] has SENDHUP but no source found for '$bin'"

		grep -q "XYMONLAUNCH_LOGFILENAME" "$src" \
			|| fail "$(basename "$cfg"): [$task] has SENDHUP, but $bin does not read \$XYMONLAUNCH_LOGFILENAME, so it cannot know which file to reopen"
		grep -qE "case SIGHUP" "$src" \
			|| fail "$(basename "$cfg"): [$task] has SENDHUP, but $bin does not handle SIGHUP at all"
		grep -q "dologswitch" "$src" \
			|| fail "$(basename "$cfg"): [$task] has SENDHUP, but $bin does not treat SIGHUP as a log switch"
	done < <(awk '
		/^\[/       { section = $0; gsub(/[][]/, "", section); cmd = ""; next }
		/^[ \t]*CMD /   { sub(/^[ \t]*CMD[ \t]+/, ""); cmd = $0; next }
		/^[ \t]*SENDHUP[ \t]*$/ { print section, cmd }
	' "$cfg")
done

# A contract that matched nothing would pass forever without saying so.
[ "$checked" -gt 0 ] || fail "no task carries SENDHUP in the shipped configs -- this test no longer checks anything"

# ---- the launcher reads what its tasks read ---------------------------------
# xymonlaunch expands each task's PIDFILE path in the parent, so it must be
# started with the settings file its tasks name in ENVFILE. The server's
# xymon.sh passes --env; without the same on the client the launcher expands
# those paths with the built-in defaults, unlinks files nobody wrote, and leaves
# the real pidfiles behind.
envfile=$(grep -oE 'ENVFILE[[:space:]]+\$XYMONCLIENTHOME/etc/[a-z.]+' "$ROOT/client/clientlaunch.cfg.DIST" | awk 'NR==1{print $2}')
[ -n "$envfile" ] || fail "no client task names an ENVFILE, so this contract has nothing to hold"
grep -q -- "--env=$envfile" "$ROOT/client/runclient.sh" \
	|| fail "runclient.sh starts xymonlaunch without --env=$envfile, so the launcher expands task pidfile paths differently from the tasks themselves"

pass "every task shipped with SENDHUP names a binary that reopens its log on SIGHUP ($checked tasks), and the client launcher reads what its tasks read"
