#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/xymonproxy-pidfile-ownership.sh
#
# xymonproxy removed a pidfile it had never written.
#
# The file is only written by the daemonising parent, in the branch guarded by
# "if (daemonize)". The removal on the way out was not guarded at all, and
# pidfile is never NULL -- it defaults to /var/run/xymonproxy.pid -- so a proxy
# started with --no-daemon deleted whatever that path named. The shipped task
# runs it exactly that way, and with a PIDFILE keyword the launcher is now the
# process that writes and removes the task's pidfile: a second process deleting
# the same kind of file behind it is the wrong shape for that contract.
#
# The daemon case is the one that must keep working: there the parent wrote the
# path holding the child's pid, and the child removes it when it stops. Both
# are checked, because guarding the unlink with the wrong condition would pass
# the first case and silently leave a stale pidfile after every daemon exit.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin XYMONPROXY "xymonproxy/xymonproxy"

work=$(mktempdir)

# A port nobody answers on: the proxy only has to come up and be signalled,
# never to deliver anything.
port=$(( 20000 + (RANDOM % 10000) ))

# --- a foreground proxy leaves a pidfile it did not write ---------------------
pid="$work/foreground.pid"
echo "not-mine" >"$pid"

"$XYMONPROXY" --server=127.0.0.1:$((port + 1)) --listen=127.0.0.1:$port \
	--no-daemon --pidfile="$pid" >"$work/fg.log" 2>&1 &
proxy=$!
register_cleanup "kill -9 $proxy 2>/dev/null || :"

# Wait for it to be listening, not merely to exist. Signalled before it is
# running, TERM kills it outright, nothing runs on the way out, and the
# assertion below passes without having tested anything.
#
# "Listening" is not proof of readiness either: the proxy prints it before
# setup_signalhandler() arms the TERM handler, so a signal in that window
# still kills it outright. Nothing is logged after the handlers go in, so
# there is no cue to wait for -- the handler having run is asserted below
# instead, which turns that race from a silent pass into a visible failure.
i=0
while [ "$i" -lt 100 ] && ! grep -q Listening "$work/fg.log" 2>/dev/null; do sleep 0.1; i=$((i + 1)); done
grep -q Listening "$work/fg.log" 2>/dev/null || fail \
	"the proxy never reported listening, so signalling it proves nothing:
$(cat "$work/fg.log")"

kill -TERM "$proxy" 2>/dev/null || fail "could not signal the foreground proxy"
i=0
while [ "$i" -lt 100 ] && kill -0 "$proxy" 2>/dev/null; do sleep 0.1; i=$((i + 1)); done
wait "$proxy" 2>/dev/null || :

grep -q "Caught TERM signal" "$work/fg.log" 2>/dev/null || fail \
	"the foreground proxy left no record of handling TERM, so it was killed
before its handler was armed and never reached its cleanup at all -- whatever
the pidfile looks like now was not decided by the code under test:
$(cat "$work/fg.log")"

[ -f "$pid" ] || fail \
	"a proxy run with --no-daemon removed $pid, which it never wrote: with
--no-daemon the pidfile is not written at all, so whatever that path named
belonged to something else -- the launcher's own file for the task, once
tasks.cfg names it with PIDFILE"
[ "$(cat "$pid")" = "not-mine" ] || fail \
	"the foreground proxy rewrote a pidfile it does not own: $(cat "$pid")"

# --- a daemonised proxy still removes its own ---------------------------------
# Non-vacuity for the guard: if it excluded the daemon too, this file would
# outlive every proxy and the next start would read a pid belonging to someone
# else.
dpid="$work/daemon.pid"
port=$((port + 2))
"$XYMONPROXY" --server=127.0.0.1:$((port + 1)) --listen=127.0.0.1:$port \
	--daemon --pidfile="$dpid" >"$work/d.log" 2>&1 || :

i=0
while [ "$i" -lt 100 ] && [ ! -s "$dpid" ]; do sleep 0.1; i=$((i + 1)); done
[ -s "$dpid" ] || skip \
	"the daemonised proxy wrote no pidfile here, so there is nothing to watch
it remove; the foreground case above is still checked"

child=$(cat "$dpid")
register_cleanup "kill -9 $child 2>/dev/null || :"
kill -TERM "$child" 2>/dev/null || fail "could not signal the daemonised proxy at pid $child"

i=0
while [ "$i" -lt 100 ] && [ -f "$dpid" ]; do sleep 0.1; i=$((i + 1)); done
grep -q "Caught TERM signal" "$work/d.log" 2>/dev/null || fail \
	"the daemonised proxy left no record of handling TERM, so it was killed
before its handler was armed; the check below would then be measuring nothing:
$(cat "$work/d.log")"
[ ! -f "$dpid" ] || fail \
	"a daemonised proxy left $dpid behind: its parent wrote that path with this
pid, so nothing else will remove it and the next start reads a pid the system
may have handed to another process"

pass "xymonproxy removes the pidfile it owns and leaves alone the one it never wrote"
