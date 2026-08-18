#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/xymondboard-color-history.sh
#
# The two colors a board consumer can ask for, and the difference between them:
#
#   prevreportcolor   the color of the previous report, assigned on every message
#   prevchangecolor   the color held before the most recent change
#
# They are the same only until a test repeats itself. A test that goes green,
# red, red reports color=red prevreportcolor=red prevchangecolor=green -- and it is
# prevchangecolor that answers "what was it before this went bad", which is what
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
	gotcolor=$(board color); gotold=$(board prevreportcolor); gotprev=$(board prevchangecolor)
	[ "$gotcolor" = "$wantcolor" ] && [ "$gotold" = "$wantold" ] && [ "$gotprev" = "$wantprev" ] \
		|| fail "$label: expected color=$wantcolor prevreportcolor=$wantold prevchangecolor=$wantprev, got color=$gotcolor prevreportcolor=$gotold prevchangecolor=$gotprev"
}

start_xymond

# --- a status nobody has seen change ------------------------------------------
# "none" is the no-previous-color sentinel, and it is not a transition. It
# stands for prevreportcolor only until the second report: prevreportcolor is assigned on
# every message, so a repeated green makes it green while prevchangecolor is
# still none -- nothing has changed yet.
status green
assert_colors green none none "first report"

status green
assert_colors green green none "a repeated color moves prevreportcolor, not prevchangecolor"

# --- restored from a checkpoint that predates the field -----------------------
# An upgrade restores a file written by a xymond that had no color history to
# save: the status record without the ".prevchangecolor." record beside it. The restore path fills
# the field from a calloc'ed struct, where 0 is COL_GREEN -- so without an
# explicit default every restored test claims it was green before a change it
# never had. Written by hand because this xymond always writes the sidecar.
stop_xymond
[ -s "$work/chk.out" ] || fail "xymond wrote no checkpoint file"
grep -v '^@@XYMONDCHK-V1|\.prevchangecolor\.|' "$work/chk.out" > "$work/chk.old"
grep -q '^@@XYMONDCHK-V1||testhost' "$work/chk.old" \
	|| fail "no status record left after dropping the record; the checkpoint format changed"
start_xymond --restart="$work/chk.old"
assert_colors green green none "a status restored without color history must not invent one"

# --- the case the field exists for --------------------------------------------
status red
assert_colors red green green "the change itself"

status red
assert_colors red red green "a repeat after the change keeps what came before it"

# --- and it follows the next change, not the reports in between ---------------
status yellow
assert_colors yellow red red "the next change moves prevchangecolor to the color that was held"

# --- across a restart ----------------------------------------------------------
# A monitoring server restarts, often during the incident the operator is
# looking at. A field whose whole job is to say what came before is worth
# nothing if it forgets exactly then, so it rides the checkpoint.
stop_xymond   # xymond writes its checkpoint while shutting down
[ -s "$work/chk.out" ] || fail "xymond wrote no checkpoint file"

grep -q '^@@XYMONDCHK-V1|\.prevchangecolor\.|testhost\.example\.com|conn|red$' "$work/chk.out" \
	|| fail "the checkpoint carries no .prevchangecolor. record for a status that has one"
start_xymond --restart="$work/chk.out"
assert_colors yellow red red "a restored status keeps the color it held before the change"

# oldcolor is kept as an alias for prevreportcolor: it is the name inside
# xymond and the one a script reaches for first. An alias nothing exercises is
# an alias that quietly stops resolving.
[ "$(board oldcolor)" = "$(board prevreportcolor)" ] \
	|| fail "the oldcolor alias must resolve to the same field as prevreportcolor"
[ "$(board previouscolor)" = "$(board prevchangecolor)" ] \
	|| fail "the previouscolor alias must resolve to the same field as prevchangecolor"

pass "xymondboard exposes prevreportcolor (alias oldcolor) and prevchangecolor, and prevchangecolor survives a restart"
