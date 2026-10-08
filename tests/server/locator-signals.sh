#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/locator-signals.sh
#
# xymond_locator must survive a HUP and still stop on a TERM.
#
# HUP is the log-rotation signal: sigmisc_handler() re-opens the log file.
# But the signal also interrupts the select() the locator waits in, and the
# main loop treated that EINTR as fatal -- "select error, aborting:
# Interrupted system call" -- so every HUP stopped the locator. The fix
# treats EINTR as what it is, and lets the loop condition decide: HUP leaves
# keeprunning set and the locator goes back to waiting; TERM clears it, and
# the loop ends and saves the state files as before.
#
# Runs the real daemon on a loopback UDP port and asks it with lib/locator,
# the interactive client built beside it, whose start-up ping prints
# "Locator is available".

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin XYMOND_LOCATOR xymond/xymond_locator
require_bin LOCATOR lib/locator

work=$(mktempdir)
export XYMONTMP="$work"

# answers PORT -- the locator on PORT replies to a ping.
answers() {
	grep -q "Locator is available" <<<"$("$LOCATOR" "127.0.0.1:$1" </dev/null 2>/dev/null)"
}

# Start the locator on a free UDP port. It exits at once when the port is
# taken ("Cannot bind"), so a collision is a retry on another port rather
# than something to probe for beforehand.
pid= PORT=
for try in 1 2 3 4 5 6 7 8 9 10; do
	p=$(( 20000 + (RANDOM % 20000) ))
	"$XYMOND_LOCATOR" --listen="127.0.0.1:$p" --logfile="$work/locator.log" &
	cand=$!
	up=
	for _ in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do
		kill -0 "$cand" 2>/dev/null || break
		if answers "$p"; then up=1; break; fi
		sleep 0.1
	done
	if [ -n "$up" ]; then pid=$cand; PORT=$p; break; fi
	kill "$cand" 2>/dev/null || true
	wait "$cand" 2>/dev/null || true
done
[ -n "$pid" ] || fail "xymond_locator did not come up on any loopback port: $(cat "$work/locator.log" 2>/dev/null)"
register_cleanup "kill $pid 2>/dev/null || true"

# HUP: re-open the log, keep running, keep answering.
kill -HUP "$pid"
for _ in 1 2 3 4 5 6 7 8 9 10; do
	grep -q "Caught SIGHUP" "$work/locator.log" && break
	sleep 0.1
done
assert_contains "Caught SIGHUP, reopening logfile" "$(cat "$work/locator.log")" \
	"xymond_locator did not handle the HUP"
sleep 0.5
kill -0 "$pid" 2>/dev/null \
	|| fail "xymond_locator exited on a HUP: $(cat "$work/locator.log")"
assert_not_contains "select error" "$(cat "$work/locator.log")" \
	"xymond_locator treated the HUP's interrupted select() as an error"
answers "$PORT" || fail "xymond_locator stopped answering after a HUP"

# TERM: still ends the loop, and the state files are written on the way out.
kill -TERM "$pid"
for _ in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do
	kill -0 "$pid" 2>/dev/null || break
	sleep 0.1
done
kill -0 "$pid" 2>/dev/null && fail "xymond_locator did not stop on a TERM"
assert_file_exists "$work/locator.servers.chk" "xymond_locator did not save its state on TERM"

pass "xymond_locator survives a HUP and still stops, saving its state, on a TERM"
