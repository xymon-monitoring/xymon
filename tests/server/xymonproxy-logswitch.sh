#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/xymonproxy-logswitch.sh
#
# Started by xymonlaunch with SENDHUP, xymonproxy gets a HUP at every log
# rotation. It must reopen the file it was given in XYMONLAUNCH_LOGFILENAME --
# it has no --logfile of its own there -- and must not count the select() the
# signal interrupts as a failure: it aborted after the sixth.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin XYMONPROXY "xymonproxy/xymonproxy"

work=$(mktempdir)
log="$work/proxy.log"

# A port nobody answers on: the proxy only has to run and be signalled.
port=$(( 20000 + (RANDOM % 10000) ))

XYMONLAUNCH_LOGFILENAME="$log" "$XYMONPROXY" --server=127.0.0.1:$((port + 1)) \
	--listen=127.0.0.1:$port --no-daemon >"$log" 2>&1 &
proxy=$!
register_cleanup "kill -9 $proxy 2>/dev/null || :"

i=0
while [ "$i" -lt 100 ] && ! grep -q Listening "$log" 2>/dev/null; do sleep 0.1; i=$((i + 1)); done
grep -q Listening "$log" 2>/dev/null || fail "the proxy never reported listening: $(cat "$log")"
# The HUP handler is armed just after "Listening" is printed; a HUP inside
# that window would kill it, which the liveness check below reports.
sleep 1

# ---- the rotated file is reopened ---------------------------------------------
mv "$log" "$log.1"
kill -HUP "$proxy"
i=0
while [ "$i" -lt 50 ] && [ ! -f "$log" ]; do sleep 0.1; i=$((i + 1)); done
[ -f "$log" ] || fail "xymonproxy did not reopen XYMONLAUNCH_LOGFILENAME after a HUP: $(cat "$log.1")"

# ---- repeated HUPs do not abort it ---------------------------------------------
for _ in 1 2 3 4 5 6 7 8 9 10; do
	kill -HUP "$proxy" 2>/dev/null || break
	sleep 0.3
done
kill -0 "$proxy" 2>/dev/null || fail "xymonproxy exited after ten HUPs: $(cat "$log" "$log.1")"
if grep -q "select() failed" "$log" "$log.1"; then
	fail "xymonproxy counted a HUP-interrupted select() as a failure: $(cat "$log")"
fi

kill "$proxy" 2>/dev/null || :
wait "$proxy" 2>/dev/null || :

pass "xymonproxy reopens XYMONLAUNCH_LOGFILENAME on a HUP and survives repeated ones"
