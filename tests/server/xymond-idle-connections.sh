#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/xymond-idle-connections.sh
#
# A connection that sends nothing cannot hold xymond's file descriptors.
#
# xymond kept every accepted connection until its message was complete or
# --timeout (30 s) ran out, so about a thousand connections that sent
# nothing used up its open-file limit and left every other client
# unanswered for 30 seconds. Now a connection that has sent nothing within
# 5 seconds is dropped, and once 256 such connections are open, at most 16
# more are taken from one sender; a message that is arriving keeps the
# whole --timeout.
#
# Checked, with xymond-idle-connections-harness.c:
#   - three connections that send nothing are closed by the time 9 seconds
#     have passed, while a status sent slowly over 7 seconds is kept --
#     xymond applies timeouts when it next has something to do, which a
#     ping at 6.5 seconds provides;
#   - 100 connections from one address, each in the middle of sending a
#     status, as a proxy has, are all kept;
#   - 100 connections from one address that send nothing, with no pressure,
#     are all kept: a burst of TLS clients still setting up looks like that;
#   - while 127.0.0.3 holds 300 that send nothing, it keeps 256, then of 100
#     from 127.0.0.2 16 stay open, and 3 from 127.0.0.1 are still taken;
#     and of 300 from ::1, to a xymond listening on [::1], 256 stay open:
#     the cap holds for an IPv6 sender too. Which addresses count as one
#     sender (one IPv6 /64) is tests/libxymon/sockaddr-same-sender.sh.
#     127.0.0.2 and .3 need more loopback addresses, which Linux has and the
#     BSDs do not, and ::1 an IPv6 loopback; where either is missing that
#     part does not run, and the result says so.
set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
# shellcheck source=tests/lib/xymond-daemon.sh
. "$(dirname "$0")/../lib/xymond-daemon.sh"

require_bin XYMOND xymond/xymond
require_bin XYMONCLIENT common/xymon
require_cc
require_shm_segments "$(grep -c 'setup_channel(C_[A-Z_]*, CHAN_MASTER)' "$(find_root)/xymond/xymond.c")"

work=$(mktempdir)
mkdir -p "$work/home/etc" "$work/home/tmp" "$work/home/www"
printf 'page test Test\n127.0.0.1 testhost # conn\n' > "$work/hosts.cfg"
"$CC" -o "$work/harness" "$(dirname "$0")/xymond-idle-connections-harness.c" 2>"$work/cc.log" \
	|| fail "the harness does not compile: $(cat "$work/cc.log")"

require_cfg XYMONSERVER_CFG xymond/etcfiles/xymonserver.cfg
sed -e 's|^XYMONHOME=.*|XYMONHOME="'"$work"'/home"|' \
    -e 's|^XYMONTMP=.*|XYMONTMP="'"$work"'/home/tmp"|' \
	"$XYMONSERVER_CFG" > "$work/xymonserver.cfg"

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

ping_xymond() { XYMON_TIMEOUT=5 "$XYMONCLIENT" "127.0.0.1:$P" ping >/dev/null 2>&1 || true; }

# Silent connections go; a slow message stays
"$work/harness" count 127.0.0.1 "$P" 3 9 > "$work/count.out" 2>&1 &
cpid=$!
"$work/harness" slow 127.0.0.1 "$P" testhost.slow 7 > "$work/slow.out" 2>&1 &
spid=$!
sleep 6.5
ping_xymond
wait "$cpid" || fail "the harness failed counting silent connections: $(cat "$work/count.out")"
wait "$spid" || fail "the harness failed sending slowly: $(cat "$work/slow.out")"
assert_contains "open 0" "$(cat "$work/count.out")" \
	"connections that sent nothing were not closed after 5 seconds; xymond's log: $(cat "$work/xymond.log")"
got=$(XYMON_TIMEOUT=5 "$XYMONCLIENT" "127.0.0.1:$P" "xymondlog testhost.slow" 2>&1 || true)
assert_contains "sent slowly" "$got" \
	"a status sent slowly over 7 seconds was dropped as if it had sent nothing; xymond's log: $(cat "$work/xymond.log")"

# A proxy's case: many connections from one address, each in the middle of
# a message. They are not silent, so none counts against the cap.
got=$("$work/harness" burst 127.0.0.1 "$P" 100 2>&1) || fail "the harness failed its burst: $got"
assert_contains "open 100" "$got" \
	"connections in the middle of a message were refused for being many from one address; xymond's log: $(cat "$work/xymond.log")"

missing=""

# No pressure: a burst of silent connections from one address is taken whole.
got=$("$work/harness" count 127.0.0.1 "$P" 100 1 2>&1) || fail "the harness failed opening connections from 127.0.0.1: $got"
assert_contains "open 100" "$got" \
	"with fewer than 256 silent connections open, some of 100 from one address were refused; xymond's log: $(cat "$work/xymond.log")"

# Pressure: 127.0.0.3 holds 300 silent connections; the 257th meets both the
# pressure and its sender's cap and is refused, which xymond logs. Then the
# cap is per sender: 127.0.0.2 gets 16, 127.0.0.1 still gets its 3.
rc=0
"$work/harness" count 127.0.0.3 "$P" 300 4 > "$work/press.out" 2>&1 &
presid=$!
for _ in $(seq 1 50); do
	grep -q 'Refused [0-9]* connection(s) from 127.0.0.3' "$work/xymond.log" && break
	kill -0 "$presid" 2>/dev/null || break
	sleep 0.1
done
capped="" other=""
if grep -q 'Refused [0-9]* connection(s) from 127.0.0.3' "$work/xymond.log"; then
	capped=$("$work/harness" count 127.0.0.2 "$P" 100 1 2>&1) \
		|| fail "the harness failed opening connections from 127.0.0.2: $capped"
	other=$("$work/harness" count 127.0.0.1 "$P" 3 1 2>&1) \
		|| fail "the harness failed opening connections from 127.0.0.1: $other"
fi
wait "$presid" || rc=$?
got=$(cat "$work/press.out")
if [ "$rc" -eq 3 ]; then
	missing="the cap from IPv4 (no further loopback address here: $got)"
else
	[ "$rc" -eq 0 ] || fail "the harness failed opening connections from 127.0.0.3: $got"
	assert_contains "open 256" "$got" \
		"of 300 silent connections from 127.0.0.3, the number kept was not 256; xymond's log: $(cat "$work/xymond.log")"
	assert_contains "open 16" "$capped" \
		"with 256 silent connections open, of 100 from 127.0.0.2 the number kept was not 16; xymond's log: $(cat "$work/xymond.log")"
	assert_contains "open 3" "$other" \
		"with 127.0.0.3 at its cap, silent connections from 127.0.0.1 were refused too: the cap is not per sender; xymond's log: $(cat "$work/xymond.log")"
fi

# The cap from ::1, to a second xymond listening there only
P6=$(free_port)
"$XYMOND" --no-daemon --no-loopback --listen="[::1]:$P6" \
	--hosts="$work/hosts.cfg" --env="$work/xymonserver.cfg" \
	--pidfile="$work/xymond6.pid" --checkpoint-file="$work/chk6.out" \
	> "$work/xymond6.log" 2>&1 &
pid6=$!
register_cleanup "stop_process $pid6"
for _ in $(seq 1 100); do
	grep -q 'Setup complete' "$work/xymond6.log" 2>/dev/null && break
	kill -0 "$pid6" 2>/dev/null || break
	sleep 0.1
done
if ! grep -q 'Setup complete' "$work/xymond6.log" 2>/dev/null; then
	grep -q 'Cannot bind to listen socket ::1' "$work/xymond6.log" \
		|| fail "xymond did not start on [::1]: $(cat "$work/xymond6.log")"
	missing="${missing:+$missing; }the cap from IPv6 (no IPv6 loopback here)"
else
	got=$("$work/harness" count ::1 "$P6" 300 2 2>&1) || fail "the harness failed opening connections from ::1: $got"
	assert_contains "open 256" "$got" \
		"of 300 silent connections from ::1, the number kept was not 256; xymond's log: $(cat "$work/xymond6.log")"
	grep -q 'Refused [0-9]* connection(s) from ::1' "$work/xymond6.log" \
		|| fail "xymond did not log the connections it refused from ::1: $(cat "$work/xymond6.log")"
fi

[ -z "$missing" ] || pass_partial "connections that send nothing are dropped after 5 seconds, a slow message is kept, and 100 messages in progress or 100 silent connections from one address are all kept" \
	"not checked: $missing"

pass "connections that send nothing are dropped after 5 seconds, and capped at 16 per sender once 256 are open, while another sender, a slow message, and 100 messages in progress or 100 silent connections from one address get through"
