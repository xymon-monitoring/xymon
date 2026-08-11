#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/xymond-holdtime-gap.sh
#
# Drives a real xymond over the gap bookkeeping that decides what `lastchange`
# says -- the field the board shows as "red for 3 days" and the one
# xymond_alert turns into `eventstart`, which alerts.cfg DURATION reads.
#
# The companion test xymond-holdtime-bridge.sh drives the pure predicate.
# Nothing there reaches the code that opens and closes a gap, so this drives
# the whole path in the daemon: a gap is set up by restoring a checkpoint that
# carries one, then real messages decide what happens to it.
#
# Restoring the state beats producing it: a real no-data gap needs xymond to
# notice a test has gone stale, which cannot happen faster than its report
# validity.
#
# What it pins:
#
#   1. A test that resumes with the color it left with keeps its hold-time.
#   2. A test that resumes with a different color starts a fresh one.
#   3. A genuine report ENDS the gap even when it does not change the color.
#      Closing only on a color change leaves the gap open, and a later change
#      then bridges across a period the test was demonstrably reporting.
#   4. A disable landing inside a gap does not destroy it. The BLUE of a
#      disable is xymond's choice, not the test reporting, so the gap must
#      survive to be resolved by the report that eventually arrives.
#   5. Both of those on a running daemon, where a report that changes nothing
#      reaches no part of the status-change path -- a restored log is the one
#      case that does not exercise.
#
# Needs a built tree: xymond itself and the xymon client.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)

require_bin XYMOND xymond/xymond
require_bin XYMONCLIENT common/xymon

work=$(mktempdir)

XYMOND_PID=""
stop_xymond() {
	[ -n "$XYMOND_PID" ] || return 0
	kill "$XYMOND_PID" 2>/dev/null || true
	local i=0
	while kill -0 "$XYMOND_PID" 2>/dev/null && [ "$i" -lt 100 ]; do
		sleep 0.1
		i=$((i+1))
	done
	XYMOND_PID=""
}
register_cleanup stop_xymond

printf 'page test Test\n127.0.0.1 testhost.example.com # conn\n' > "$work/hosts.cfg"

# xymond will not start unless XYMONHOME is a real directory, and the shipped
# xymonserver.cfg names the install prefix, which need not exist here.
mkdir -p "$work/home/etc" "$work/home/tmp" "$work/home/www"
sed -e 's|^XYMONHOME=.*|XYMONHOME="'"$work"'/home"|' \
    -e 's|^XYMONTMP=.*|XYMONTMP="'"$work"'/home/tmp"|' \
	"$ROOT/xymond/etcfiles/xymonserver.cfg" > "$work/xymonserver.cfg" \
	|| skip "no xymonserver.cfg to run against"

free_port() {
	local p tries=0
	while [ "$tries" -lt 50 ]; do
		p=$(( 20000 + (RANDOM % 20000) ))
		"$XYMONCLIENT" "127.0.0.1:$p" "ping" >/dev/null 2>&1 || { printf '%s' "$p"; return 0; }
		tries=$((tries+1))
	done
	return 1
}

# write_checkpoint HELDCOLOR HELDSINCE GAPCOLOR GAPSTART -- a checkpoint where
# conn currently shows HELDCOLOR (since HELDSINCE) with a gap open: xymond
# invented HELDCOLOR, and the color before the gap was GAPCOLOR, held from
# HELDSINCE. Field order matches save_checkpoint().
write_checkpoint() {
	local shown=$1 shownsince=$2 gapcolor=$3 gapstart=$4
	local valid=$(( $(date +%s) + 86400 ))

	printf '@@XYMONDCHK-V1||testhost.example.com|conn|127.0.0.1|%s||%s|%s|%s|%s|0|0|0|0|status testhost,example,com.conn %s invented|||0|0\n' \
		"$shown" "$gapcolor" "$shownsince" "$shownsince" "$valid" "$shown" > "$work/chk"
	printf '@@XYMONDCHK-V1|.flapstate.|testhost.example.com|conn|0|none|none|%s|%s|%s|0,0,0,0,0\n' \
		"$gapcolor" "$HELDSINCE" "$gapstart" >> "$work/chk"
}

# start_xymond [extra xymond arguments] -- the cases that need a gap already in
# place pass --restart; the ones that build it from live messages do not.
start_xymond() {
	local i=0
	PORT=$(free_port) || fail "no free port for xymond"
	"$XYMOND" --no-daemon --listen="127.0.0.1:$PORT" \
		--hosts="$work/hosts.cfg" --env="$work/xymonserver.cfg" \
		--pidfile="$work/xymond.pid" --checkpoint-file="$work/chk.out" \
		"$@" \
		> "$work/xymond.log" 2>&1 &
	XYMOND_PID=$!

	while [ "$i" -lt 100 ]; do
		"$XYMONCLIENT" "127.0.0.1:$PORT" "ping" >/dev/null 2>&1 && return 0
		kill -0 "$XYMOND_PID" 2>/dev/null || { cat "$work/xymond.log" >&2; fail "xymond exited during startup"; }
		sleep 0.1
		i=$((i+1))
	done
	cat "$work/xymond.log" >&2
	fail "xymond did not answer on 127.0.0.1:$PORT"
}

send() { "$XYMONCLIENT" "127.0.0.1:$PORT" "$1" || fail "xymond rejected: $1"; }
status() { send "status testhost,example,com.conn $1 msg"; sleep 0.4; }

lastchange() {
	"$XYMONCLIENT" "127.0.0.1:$PORT" "xymondboard fields=testname,lastchange" 2>/dev/null \
		| awk -F'|' '$1 == "conn" { print $2 }'
}

xymondboard_color() {
	"$XYMONCLIENT" "127.0.0.1:$PORT" "xymondboard fields=testname,color" 2>/dev/null \
		| awk -F'|' '$1 == "conn" { print $2 }'
}

# assert_held WHEN LABEL -- lastchange must be WHEN (the hold-time carried).
assert_held() {
	local got; got=$(lastchange)
	[ "$got" = "$1" ] || fail "$2: expected lastchange=$1 (hold-time carried), got $got"
}

# assert_moved WHEN LABEL -- lastchange must have left WHEN. The live cases
# build their own hold-times seconds ago, which assert_fresh cannot tell from a
# carried one; they separate the two by a sleep and compare exactly.
assert_moved() {
	local got; got=$(lastchange)
	[ "$got" != "$1" ] \
		|| fail "$2: lastchange is still $1, carried across a gap the test had already ended"
}

# assert_fresh LABEL -- lastchange must be recent (a new hold-time started).
assert_fresh() {
	local got age; got=$(lastchange)
	[ -n "$got" ] || fail "$2${1}: conn is not on the board"
	age=$(( $(date +%s) - got ))
	[ "$age" -ge 0 ] && [ "$age" -lt 300 ] \
		|| fail "$1: expected a fresh lastchange, got one ${age}s old (hold-time was carried when it should not be)"
}

# ---- 1. resume with the same color: the hold-time carries -------------------

HELDSINCE=$(( $(date +%s) - 86400 ))
write_checkpoint clear "$(( $(date +%s) - 60 ))" red "$(( $(date +%s) - 60 ))"
start_xymond --restart="$work/chk"
status red
assert_held "$HELDSINCE" "same-color resume"
stop_xymond

# ---- 2. resume with a different color: a fresh hold-time -------------------

HELDSINCE=$(( $(date +%s) - 86400 ))
write_checkpoint clear "$(( $(date +%s) - 60 ))" red "$(( $(date +%s) - 60 ))"
start_xymond --restart="$work/chk"
status yellow
assert_fresh "different-color resume"
stop_xymond

# ---- 3. a genuine report ends the gap even without a color change ----------
#
# xymond invented CLEAR while the test was quiet. The test comes back and
# genuinely reports CLEAR, and only later goes red. That red began just now; it
# must not inherit the hold-time from before a silence the test has already
# ended.
#
# A restored log has histsynced == 0, so its first report enters the
# status-change path whatever color it carries. Case 6 drives the same sequence
# on a running daemon, where it does not.

HELDSINCE=$(( $(date +%s) - 86400 ))
write_checkpoint clear "$(( $(date +%s) - 60 ))" red "$(( $(date +%s) - 60 ))"
start_xymond --restart="$work/chk"
status clear
status red
assert_fresh "report matching the invented color must end the gap"
stop_xymond

# ---- 4. a disable inside the gap must not destroy it ------------------------
#
# The BLUE of a disable is xymond's choice, not the test reporting, so it must
# leave the gap alone; the report that arrives after the enable is what
# resolves it.

HELDSINCE=$(( $(date +%s) - 86400 ))
write_checkpoint clear "$(( $(date +%s) - 60 ))" red "$(( $(date +%s) - 60 ))"
start_xymond --restart="$work/chk"
send "disable testhost.example.com.conn 5 investigating"
sleep 0.4
send "enable testhost.example.com.conn"
sleep 0.4
status red
assert_held "$HELDSINCE" "disable inside a gap"
stop_xymond

# ---- 5. a DOWNTIME window hides the real color, so it opens a gap ----------
#
# During DOWNTIME the test keeps reporting but xymond records BLUE, so the real
# color is just as hidden as during a silence. The gap bookkeeping used to sit
# inside the "not in DOWNTIME" guard, so no window could ever open one and the
# hold-time was lost when the window ended.
#
# The window cannot be ended from here (it is a hosts.cfg tag), so this checks
# the half that is observable: the gap opens, carrying the color and hold-time
# from before the window.

HELDSINCE=$(( $(date +%s) - 86400 ))
printf 'page test Test\n127.0.0.1 testhost.example.com # conn DOWNTIME=*:*:0000:2359:Maint\n' > "$work/hosts.cfg"
printf '@@XYMONDCHK-V1||testhost.example.com|conn|127.0.0.1|red||red|%s|%s|%s|0|0|0|0|status testhost,example,com.conn red held|||0|0\n' \
	"$HELDSINCE" "$HELDSINCE" "$(( $(date +%s) + 86400 ))" > "$work/chk"

start_xymond --restart="$work/chk"
status red
[ "$(xymondboard_color)" = "blue" ] || fail "DOWNTIME did not force the status blue; the tag was not applied"
stop_xymond

flapstate=$(grep '^@@XYMONDCHK-V1|\.flapstate\.|' "$work/chk.out" || true)
[ -n "$flapstate" ] \
	|| fail "a DOWNTIME window opened no gap, so the hold-time is lost when it ends"
gapcolor=$(printf '%s' "$flapstate" | awk -F'|' '{ print $8 }')
gapheld=$(printf '%s' "$flapstate" | awk -F'|' '{ print $9 }')
[ "$gapcolor" = "red" ] \
	|| fail "gap opened by DOWNTIME recorded the color as $gapcolor, expected the pre-window red"
[ "$gapheld" = "$HELDSINCE" ] \
	|| fail "gap opened by DOWNTIME recorded hold-time $gapheld, expected $HELDSINCE"

# ---- 6. the same, on a running daemon --------------------------------------
#
# Cases 1-5 restore their gap, and a restored log takes the status-change path
# on its first report whatever that report says. A running xymond has
# histsynced == 1, and there a report that changes nothing takes no path at
# all -- so the gap has to end outside that one. The disable opens the gap
# because a no-data gap needs the purple sweep, which does not run until ten
# minutes after startup.

printf 'page test Test\n127.0.0.1 testhost.example.com # conn\n' > "$work/hosts.cfg"
rm -f "$work/chk"
start_xymond
status red
HELDSINCE=$(lastchange)
sleep 2
send "disable testhost.example.com.conn 5 investigating"
sleep 0.4
send "enable testhost.example.com.conn"
sleep 0.4
status blue	# genuine, and BLUE is exactly what the disable invented
status red
assert_moved "$HELDSINCE" "genuine report of the invented color must end the gap on a running daemon"
stop_xymond

# ---- 7. reports that arrive during a disable do not end the gap ------------
#
# They are genuine, but xymond records BLUE for them, so they say nothing about
# the color the test is in and the gap must survive them: the test never
# stopped being red, so the red after the enable still holds from before.
# Case 4 does not reach this -- handle_enadis() posts a status for the disable
# but not for the enable, so nothing reports inside its window.

rm -f "$work/chk"
start_xymond
status red
HELDSINCE=$(lastchange)
sleep 2
send "disable testhost.example.com.conn 5 investigating"
sleep 0.4
status red	# genuine, but recorded as the disable's BLUE
send "enable testhost.example.com.conn"
sleep 0.4
status red
assert_held "$HELDSINCE" "reports arriving during a disable must not end the gap"
stop_xymond

pass "hold-time survives a same-color resume, and only that: a different color, an ended gap on a restored and on a running daemon, a disable with and without reports inside it, and a DOWNTIME window are each handled"
