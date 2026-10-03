#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/xymonfetch-logswitch.sh
#
# Started by xymonlaunch with SENDHUP, xymonfetch gets a HUP at every log
# rotation. It has no --logfile option, so it must reopen the file the
# launcher named in XYMONLAUNCH_LOGFILENAME; it used to take the HUP only as
# "re-read hosts.cfg" and kept writing to the rotated file.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin XYMONFETCH "xymond/xymonfetch"

work=$(mktempdir)
log="$work/fetch.log"
: >"$work/hosts.cfg"

# No host is pulled from, so no server is contacted. HOSTSCFG starts with "!"
# so the hosts are read from the file, not asked of a xymond on this host.
port=$(( 20000 + (RANDOM % 10000) ))
env -i PATH="$PATH" XYMONHOME="$work" HOSTSCFG="!$work/hosts.cfg" \
	XYMONLAUNCH_LOGFILENAME="$log" \
	"$XYMONFETCH" --server=127.0.0.1:$port >"$log" 2>&1 &
fetch=$!
register_cleanup "kill -9 $fetch 2>/dev/null || :"

# It logs nothing at startup; give it time to arm its handlers. A HUP before
# that kills it, which the liveness check below reports.
sleep 1
kill -0 "$fetch" 2>/dev/null || fail "xymonfetch did not start: $(cat "$log")"

mv "$log" "$log.1"
kill -HUP "$fetch"
i=0
while [ "$i" -lt 50 ] && [ ! -f "$log" ]; do sleep 0.1; i=$((i + 1)); done
kill -0 "$fetch" 2>/dev/null || fail "xymonfetch exited on a HUP: $(cat "$log.1")"
[ -f "$log" ] || fail "xymonfetch did not reopen XYMONLAUNCH_LOGFILENAME after a HUP: $(cat "$log.1")"

kill "$fetch" 2>/dev/null || :
wait "$fetch" 2>/dev/null || :

pass "xymonfetch reopens XYMONLAUNCH_LOGFILENAME on a HUP"
