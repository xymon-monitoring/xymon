#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/xymond-port-retry.sh
#
# start_xymond() retries a port that turned out to be taken, and every startup
# gets that retry budget in full.
#
# Choosing a port cannot be made safe: the probe in free_port() only sees a
# listener that answers a Xymon ping, so a socket held by anything else reads
# as free, and on OpenBSD there is no unprivileged room below the ephemeral
# range to draw from at all. The bind retry is therefore the mechanism, not
# the fallback -- which is why it is worth a test of its own.
#
# The budget half is the regression: with one counter for the whole script,
# a test that starts the daemon a dozen times arrives at its last startup with
# the retries already spent by earlier ones, and fails on a collision it was
# meant to survive. Two startups are driven here, each needing three retries:
# six in total, past a budget of five, so a shared counter fails the second.
#
# free_port() is replaced with a scripted sequence, and the port it hands out
# first is held by tests/lib/port-blocker.c: a socket bound but never listened
# on, so the port is reserved -- an exact same-address:port bind is refused even
# with SO_REUSEADDR -- while a connect() to it is refused outright, so the
# readiness probe, which cannot tell one Xymon-speaking listener from another,
# cannot mistake it for a started daemon.
#
# That the port is reserved is checked rather than assumed. It is the premise
# the whole test rests on, and it is platform-dependent: a bind without listen
# reserves the port on Linux but not on the BSDs, where SO_REUSEADDR lets a
# second bind through while nothing is listening. Without the check, a blocker
# that fails to block does not say so -- xymond takes the port it was supposed
# to collide with, starts, and the failure arrives ten seconds later as
# "xymond did not answer", pointing at the daemon instead of at the fixture.
#
# Needs a built tree: xymond itself and the xymon client.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
# shellcheck source=tests/lib/xymond-daemon.sh
. "$(dirname "$0")/../lib/xymond-daemon.sh"

require_bin XYMOND xymond/xymond
require_bin XYMONCLIENT common/xymon
require_shm_segments "$(grep -c 'setup_channel(C_[A-Z_]*, CHAN_MASTER)' "$(find_root)/xymond/xymond.c")"
require_c_buildenv "$(find_root)"

work=$(mktempdir)
mkdir -p "$work/home/etc" "$work/home/tmp" "$work/home/www"
printf 'page test Test\n127.0.0.1 testhost.example.com # conn\n' > "$work/hosts.cfg"

require_cfg XYMONSERVER_CFG xymond/etcfiles/xymonserver.cfg
sed -e 's|^XYMONHOME=.*|XYMONHOME="'"$work"'/home"|' \
    -e 's|^XYMONTMP=.*|XYMONTMP="'"$work"'/home/tmp"|' \
	"$XYMONSERVER_CFG" > "$work/xymonserver.cfg"

xymond_launch() {
	local port=$1; shift
	"$XYMOND" --no-daemon --listen="127.0.0.1:$port" \
		--hosts="$work/hosts.cfg" --env="$work/xymonserver.cfg" \
		--pidfile="$work/xymond.pid" --checkpoint-file="$work/chk.out" \
		"$@" \
		> "$work/xymond.log" 2>&1 &
	XYMOND_PID=$!
}

# --- the blocker: a socket holding one port, refusing connections -------------

"$CC" -o "$work/port-blocker" "$(find_root)/tests/lib/port-blocker.c" 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; skip "port-blocker does not compile"; }
"$work/port-blocker" > "$work/blocked.port" &
BLOCKER_PID=$!
register_cleanup "kill $BLOCKER_PID 2>/dev/null || true"

i=0
while [ "$i" -lt 50 ]; do
	[ -s "$work/blocked.port" ] && break
	kill -0 "$BLOCKER_PID" 2>/dev/null || fail "port-blocker exited before naming its port"
	sleep 0.1
	i=$((i+1))
done
[ -s "$work/blocked.port" ] || fail "port-blocker never named its port"
BLOCKER_PORT=$(cat "$work/blocked.port")

# The premise: nothing else may bind that port. Asked the way xymond asks,
# SO_REUSEADDR and all.
if "$work/port-blocker" "$BLOCKER_PORT"; then
	fail "port $BLOCKER_PORT is still bindable while port-blocker holds it, so the collision this test drives would never happen"
fi

# --- free_port hands out the blocked port three times, then a real one --------

# Keep the genuine picker under a second name before scripting the one the
# driver calls.
eval "real_free_port() $(declare -f free_port | tail -n +2)"

# The counter lives in a file: start_xymond calls free_port in a command
# substitution, so a shell variable it set would be lost with the subshell.
echo 0 > "$work/fp.calls"
free_port() {
	local n
	n=$(( $(cat "$work/fp.calls") + 1 ))
	echo "$n" > "$work/fp.calls"
	case "$n" in
		1|2|3|5|6|7) printf '%s' "$BLOCKER_PORT" ;;
		*) real_free_port ;;
	esac
}
free_port_calls() { cat "$work/fp.calls"; }

# --- startup 1: three collisions, then success --------------------------------

start_xymond
[ "$PORT" != "$BLOCKER_PORT" ] || fail "xymond claims to run on the blocked port $BLOCKER_PORT"
assert_equal "4" "$(free_port_calls)" \
	"the first startup must retry the blocked port three times, then take the fourth"
"$XYMONCLIENT" "127.0.0.1:$PORT" "ping" >/dev/null 2>&1 \
	|| fail "xymond did not answer on the port it recovered onto ($PORT)"

kill "$XYMOND_PID" 2>/dev/null || true
wait "$XYMOND_PID" 2>/dev/null || true

# --- startup 2: three more collisions. Six in total, past a budget of five ----

start_xymond
[ "$PORT" != "$BLOCKER_PORT" ] || fail "xymond claims to run on the blocked port $BLOCKER_PORT"
assert_equal "8" "$(free_port_calls)" \
	"the second startup must get a fresh budget, not the leftovers of the first"
"$XYMONCLIENT" "127.0.0.1:$PORT" "ping" >/dev/null 2>&1 \
	|| fail "the second startup did not answer on the port it recovered onto ($PORT)"

kill "$XYMOND_PID" 2>/dev/null || true
wait "$XYMOND_PID" 2>/dev/null || true

pass "start_xymond retries a taken port, and each startup gets the retry budget in full"
