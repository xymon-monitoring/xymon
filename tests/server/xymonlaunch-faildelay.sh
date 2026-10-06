#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/xymonlaunch-faildelay.sh
#
# A task that fails more than five times in a row is held back for FAILDELAY
# seconds from its last start, then released and tried again; FAILDELAY 0 turns
# the hold off. The hold is logged when it begins and when it ends.
#
# The launcher starts a task at most once in five seconds, so six failures
# take 25 seconds whatever else happens. Its passes come every five seconds
# too, and one that lands late pushes a retry past FAILDELAY: a failed task
# whose last start is FAILDELAY old is released, and the failures never add
# up. So the test sends a HUP every 0.3 seconds, which ends the wait between
# passes and puts each retry at five seconds after the last start, and uses
# FAILDELAY 8 -- three seconds of margin for a loaded machine. The HUPs change
# nothing else here: the config is unchanged, so the re-read they ask for
# returns early, and the log is reopened in append mode.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin XYMONLAUNCH "common/xymonlaunch"

work=$(mktempdir)
cfg="$work/tasks.cfg"
log="$work/launch.log"

# A task that fails at once, counting its starts.
cat >"$work/fail.sh" <<'EOF'
#!/bin/sh
echo start >>"$1"
exit 1
EOF
chmod +x "$work/fail.sh"

cat >"$cfg" <<EOF
[held]
	CMD $work/fail.sh $work/held
	FAILDELAY 8
[free]
	CMD $work/fail.sh $work/free
	FAILDELAY 0
[short]
	CMD $work/fail.sh $work/short
	FAILDELAY 3
EOF

wait_for() {
	local deadline=$((SECONDS + $1)); shift
	while [ "$SECONDS" -lt "$deadline" ]; do
		if eval "$@"; then return 0; fi
		sleep 0.1
	done
	return 1
}
starts() { if [ -f "$1" ]; then grep -c start "$1"; else echo 0; fi; }

"$XYMONLAUNCH" --config="$cfg" --log="$log" --no-daemon &
launcher=$!
register_cleanup "kill $launcher 2>/dev/null || true"
# Not before the launcher has started a task: that happens in its main loop,
# after its HUP handler is in place, and a HUP before it would kill it.
wait_for 20 '[ "$(starts "$work/held")" -ge 1 ]' \
	|| fail "xymonlaunch never started [held]: $(cat "$log")"
( while kill -HUP "$launcher" 2>/dev/null; do sleep 0.3; done ) &
pump=$!
register_cleanup "kill $pump 2>/dev/null || true"

# ---- six failures put [held] on hold ------------------------------------------
wait_for 30 'grep -q "Postponing restart of \[held\] for 8 seconds" "$log"' \
	|| fail "[held] failed $(starts "$work/held") times and was never put on hold: $(cat "$log")"
n=$(starts "$work/held")
[ "$n" -gt 5 ] || fail "[held] was put on hold after only $n failures"

# ---- it is not started while held, and then it is released -------------------
# The count is read before the log is: if the release is not logged yet, the
# restart that follows it had not happened when the count was taken either.
deadline=$((SECONDS + 30))
while :; do
	c=$(starts "$work/held")
	grep -q "Releasing \[held\] from failure hold" "$log" && break
	[ "$c" -eq "$n" ] || fail "[held] was started again while on hold ($n starts, then $c)"
	[ "$SECONDS" -lt "$deadline" ] || fail "[held] was never released from its hold: $(cat "$log")"
	sleep 0.1
done
wait_for 10 '[ "$(starts "$work/held")" -gt '"$n"' ]' \
	|| fail "[held] was released but not started again"

# ---- FAILDELAY 0: no hold at all ------------------------------------------------
# The two tasks fail in step, so while [held] sat out its hold with $n starts,
# [free] kept going -- unless it was held too.
[ "$(starts "$work/free")" -gt "$n" ] \
	|| fail "[free] (FAILDELAY 0) stopped at $(starts "$work/free") starts while [held] was held at $n, so it was held too"
if grep -q "\[free\]" "$log"; then
	fail "[free] (FAILDELAY 0) was put on hold: $(grep "\[free\]" "$log")"
fi

# ---- a FAILDELAY under five seconds never holds, and does not say it does ----
# It is released at every retry, before the failures can add up -- and the
# launcher used to log "Releasing [short] from failure hold" at each of them,
# for a task that had never been held.
[ "$(starts "$work/short")" -gt "$n" ] \
	|| fail "[short] (FAILDELAY 3) stopped at $(starts "$work/short") starts, so it was held"
if grep -q "\[short\]" "$log"; then
	fail "[short] (FAILDELAY 3) was reported as held, though nothing held it: $(grep "\[short\]" "$log")"
fi

kill "$pump" 2>/dev/null || true
kill "$launcher" 2>/dev/null || true
wait "$launcher" 2>/dev/null || true

pass "xymonlaunch holds a repeatedly failing task for FAILDELAY seconds and releases it; FAILDELAY 0, or under five seconds, never holds"
