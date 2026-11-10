#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/xymond-maxconnections.sh
#
# xymond holds at most XYMOND_MAXCONNECTIONS connections open at once, and
# refuses the next one as it is accepted instead of dying in its event loop
# (#760: with select(), the 257th connection on NetBSD ended xymond).
#
# Checked, with xymond-idle-connections-harness.c: with the limit set to 40,
# of 60 silent connections from 127.0.0.1 exactly 40 stay open, xymond logs
# the refusal naming the limit and is still running; once they close, 3 new
# connections are taken, so a refusal frees nothing and blocks nothing later.
# The log counts all 20 refusals, the last 19 a minute after the first.
# A limit that is not a number is logged and the default taken, without
# stopping xymond: under "ulimit -n 200", 150 connections leave 135 open (200
# less the reserve of 64 and one listener). A trailing space is not a typo.
# The limit is below the 256 at which the idle-connection cap engages, so the
# two do not meet here.

set -euo pipefail
. "$(dirname "$0")/../lib/assert.sh"
. "$(dirname "$0")/../lib/xymond-daemon.sh"

require_bin XYMOND xymond/xymond
require_bin XYMONCLIENT common/xymon
require_shm_segments "$(grep -c 'setup_channel(C_[A-Z_]*, CHAN_MASTER)' "$(find_root)/xymond/xymond.c")"
CC=${CC:-cc}
command -v "$CC" >/dev/null 2>&1 || skip "no C compiler for the harness"

work=$(mktempdir); register_cleanup "rm -rf '$work'"
mkdir -p "$work/home/tmp"
printf '127.0.0.1 testhost # conn\n' > "$work/hosts.cfg"

"$CC" -o "$work/harness" "$(dirname "$0")/xymond-idle-connections-harness.c" 2>"$work/cc.log" \
	|| fail "the harness does not compile: $(cat "$work/cc.log")"

require_cfg XYMONSERVER_CFG xymond/etcfiles/xymonserver.cfg
mkcfg() {  # mkcfg LIMIT -- the env file with XYMOND_MAXCONNECTIONS set to LIMIT
	sed -e 's|^XYMONHOME=.*|XYMONHOME="'"$work"'/home"|' \
	    -e 's|^XYMONTMP=.*|XYMONTMP="'"$work"'/home/tmp"|' \
		"$XYMONSERVER_CFG" > "$work/xymonserver.cfg"
	printf 'XYMOND_MAXCONNECTIONS="%s"\n' "$1" >> "$work/xymonserver.cfg"
}

# A limit that is not a number: xymond logs it with the default, and runs on
# that default. The open-file limit is lowered so the default is known.
mkcfg forty
P=$(free_port)
( ulimit -n 200 && exec "$XYMOND" --no-daemon --listen="127.0.0.1:$P" --hosts="$work/hosts.cfg" \
	--env="$work/xymonserver.cfg" --pidfile="$work/bad.pid" --checkpoint-file="$work/bad.chk" ) > "$work/bad.log" 2>&1 &
badpid=$!
register_cleanup "stop_process $badpid"
for _ in $(seq 1 100); do
	grep -q 'Setup complete' "$work/bad.log" 2>/dev/null && break
	kill -0 "$badpid" 2>/dev/null || fail "xymond did not start with XYMOND_MAXCONNECTIONS=forty: $(cat "$work/bad.log")"
	sleep 0.1
done
assert_contains 'XYMOND_MAXCONNECTIONS is "forty", not a number of connections above 0: using the default, 135' "$(cat "$work/bad.log")" \
	"a limit that is not a number was not logged with the default it falls back to"
got=$("$work/harness" count 127.0.0.1 "$P" 150 2 2>&1) || fail "the harness failed opening 150 connections: $got"
assert_contains "open 135" "$got" "with XYMOND_MAXCONNECTIONS=forty and 200 descriptors, the default of 135 was not held; xymond's log: $(cat "$work/bad.log")"
stop_process "$badpid"

# The limit at 40, written with a trailing space: of 60 silent connections,
# 40 stay open
mkcfg "40 "
P=$(free_port)
"$XYMOND" --no-daemon --listen="127.0.0.1:$P" \
	--hosts="$work/hosts.cfg" --env="$work/xymonserver.cfg" \
	--pidfile="$work/xymond.pid" --checkpoint-file="$work/chk.out" \
	> "$work/xymond.log" 2>&1 &
pid=$!
register_cleanup "stop_process $pid"
for _ in $(seq 1 100); do
	grep -q 'Setup complete' "$work/xymond.log" 2>/dev/null && break
	kill -0 "$pid" 2>/dev/null || fail "xymond did not start: $(cat "$work/xymond.log")"
	sleep 0.1
done

got=$("$work/harness" count 127.0.0.1 "$P" 60 2 2>&1) || fail "the harness failed opening 60 connections: $got"
assert_contains "open 40" "$got" "with XYMOND_MAXCONNECTIONS=40, of 60 silent connections the number kept was not 40; xymond's log: $(cat "$work/xymond.log")"
assert_match 'Refused [0-9]+ connection\(s\) from 127\.0\.0\.1: 40 connections are open, the limit \(XYMOND_MAXCONNECTIONS\)' "$(cat "$work/xymond.log")" \
	"the refusal was not logged with the limit"
kill -0 "$pid" 2>/dev/null || fail "xymond stopped after refusing connections: $(cat "$work/xymond.log")"

# All 20 refusals are logged: the first at once, the other 19 once the minute
# is up, although no refusal follows to trigger it. Before, the log said
# "Refused 1" and the rest of the burst was never counted.
refusedsum() {
	sed -n 's/.*Refused \([0-9]*\) connection(s) from 127\.0\.0\.1: [0-9]* connections are open, the limit.*/\1/p' "$work/xymond.log" |
		awk '{ n += $1 } END { print n + 0 }'
}
deadline=$(( $(date +%s) + 75 ))
while [ "$(refusedsum)" -lt 20 ] && [ "$(date +%s)" -lt "$deadline" ]; do sleep 1; done
[ "$(refusedsum)" -eq 20 ] \
	|| fail "the log counts $(refusedsum) of the 20 refused connections: $(grep 'Refused' "$work/xymond.log")"

# Once they are closed, new connections are taken again
got=$("$work/harness" count 127.0.0.1 "$P" 3 1 2>&1) || fail "the harness failed opening 3 connections: $got"
assert_contains "open 3" "$got" "after the 60 closed, 3 new connections were not all taken: $got"

pass "xymond keeps XYMOND_MAXCONNECTIONS connections and refuses the rest, logging all of them, without stopping; a value that is not a number is logged and the default held"
