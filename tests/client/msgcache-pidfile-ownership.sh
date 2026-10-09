#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/client/msgcache-pidfile-ownership.sh
#
# msgcache removed a pidfile it had never written. Only the daemonising parent
# writes one, but the removal on the way out was unconditional, and pidfile
# defaults to "msgcache.pid" -- so the shipped task, run with --no-daemon and
# its pidfile written by the launcher, deleted whatever that path named. It
# now removes the file only when it holds its own pid, which keeps the daemon
# case: there the parent wrote the child's pid, and the child removes it.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin MSGCACHE "client/msgcache"

work=$(mktempdir)

# A port nobody talks to: msgcache only has to come up and be signalled.
port=$(( 20000 + (RANDOM % 10000) ))

# Wait until it listens, then until it has handled TERM: "Listening" is
# printed before the TERM handler is armed, so a signal in that window kills
# it outright and the pidfile check below would decide nothing.
stop_and_check() {
	local pid=$1 log=$2 i=0
	while [ "$i" -lt 100 ] && ! grep -q Listening "$log" 2>/dev/null; do sleep 0.1; i=$((i + 1)); done
	grep -q Listening "$log" 2>/dev/null || fail "msgcache never reported listening: $(cat "$log")"
	sleep 1
	kill -TERM "$pid" 2>/dev/null || fail "could not signal msgcache"
	i=0
	while [ "$i" -lt 100 ] && kill -0 "$pid" 2>/dev/null; do sleep 0.1; i=$((i + 1)); done
	grep -q "Caught TERM signal" "$log" 2>/dev/null \
		|| fail "msgcache left no record of handling TERM, so its cleanup never ran: $(cat "$log")"
}

# ---- in the foreground it leaves alone a pidfile it did not write ------------
echo "not-mine" >"$work/fg.pid"
"$MSGCACHE" --listen=127.0.0.1:$port --no-daemon --pidfile="$work/fg.pid" >"$work/fg.log" 2>&1 &
fg=$!
register_cleanup "kill -9 $fg 2>/dev/null || :"
stop_and_check "$fg" "$work/fg.log"
wait "$fg" 2>/dev/null || :
[ -f "$work/fg.pid" ] || fail "msgcache run with --no-daemon removed $work/fg.pid, which it never wrote"
[ "$(cat "$work/fg.pid")" = "not-mine" ] || fail "msgcache rewrote a pidfile it does not own"

# ---- as a daemon it still removes its own ------------------------------------
port=$((port + 1))
"$MSGCACHE" --listen=127.0.0.1:$port --daemon --pidfile="$work/d.pid" >"$work/d.log" 2>&1 || :
i=0
while [ "$i" -lt 100 ] && [ ! -s "$work/d.pid" ]; do sleep 0.1; i=$((i + 1)); done
[ -s "$work/d.pid" ] || fail "the daemonised msgcache wrote no pidfile: $(cat "$work/d.log")"
child=$(cat "$work/d.pid")
register_cleanup "kill -9 $child 2>/dev/null || :"
stop_and_check "$child" "$work/d.log"
i=0
while [ "$i" -lt 50 ] && [ -f "$work/d.pid" ]; do sleep 0.1; i=$((i + 1)); done
[ ! -f "$work/d.pid" ] || fail "a daemonised msgcache left its own pidfile behind"

pass "msgcache removes the pidfile it owns and leaves alone the one it never wrote"
