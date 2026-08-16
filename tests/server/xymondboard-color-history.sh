#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/xymondboard-color-history.sh
#
# The two colors a board consumer can ask for, and the difference between them:
#
#   oldcolor        the color of the previous report, assigned on every message
#   previouscolor   the color held before the most recent change
#
# They are the same only until a test repeats itself. A test that goes green,
# red, red reports color=red oldcolor=red previouscolor=green -- and it is
# previouscolor that answers "what was it before this went bad", which is what
# a display or an alert rule wants (@SoundGoof, #355).
#
# Both are selectable fields now. They were carried on xymond's internal
# channels before this, so a module could see them and a `fields=` consumer
# could not.
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

# board FIELD -- one field of the conn status, through the fields= selector a
# consumer uses. An unknown field name yields an empty column, which is exactly
# what this test is here to catch.
board() {
	"$XYMONCLIENT" "127.0.0.1:$PORT" "xymondboard fields=testname,$1" 2>/dev/null \
		| awk -F'|' '$1 == "conn" { print $2 }'
}

assert_colors() {
	local wantcolor=$1 wantold=$2 wantprev=$3 label=$4
	local gotcolor gotold gotprev
	gotcolor=$(board color); gotold=$(board oldcolor); gotprev=$(board previouscolor)
	[ "$gotcolor" = "$wantcolor" ] && [ "$gotold" = "$wantold" ] && [ "$gotprev" = "$wantprev" ] \
		|| fail "$label: expected color=$wantcolor oldcolor=$wantold previouscolor=$wantprev, got color=$gotcolor oldcolor=$gotold previouscolor=$gotprev"
}

start_xymond

# --- a status nobody has seen change ------------------------------------------
# "none" is the no-previous-color sentinel, and it is not a transition. It
# stands for oldcolor only until the second report: oldcolor is assigned on
# every message, so a repeated green makes it green while previouscolor is
# still none -- nothing has changed yet.
status green
assert_colors green none none "first report"

status green
assert_colors green green none "a repeated color moves oldcolor, not previouscolor"

# --- the case the field exists for --------------------------------------------
status red
assert_colors red green green "the change itself"

status red
assert_colors red red green "a repeat after the change keeps what came before it"

# --- and it follows the next change, not the reports in between ---------------
status yellow
assert_colors yellow red red "the next change moves previouscolor to the color that was held"

# --- across a restart ----------------------------------------------------------
# A monitoring server restarts, often during the incident the operator is
# looking at. A field whose whole job is to say what came before is worth
# nothing if it forgets exactly then, so it rides the checkpoint.
stop_xymond   # xymond writes its checkpoint while shutting down
[ -s "$work/chk.out" ] || fail "xymond wrote no checkpoint file"

start_xymond --restart="$work/chk.out"
assert_colors yellow red red "a restored status keeps the color it held before the change"

pass "xymondboard exposes oldcolor and previouscolor, and previouscolor survives a restart"
