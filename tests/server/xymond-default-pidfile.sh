#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/xymond-default-pidfile.sh
#
# Started without --pidfile=, xymond writes its pid to $XYMONRUNDIR/xymond.pid
# -- where xymon.sh reload looks for it -- and removes it when it stops. It
# was the log directory. A directory too long for that path is reported, and
# xymond still runs and stops cleanly. That pins the report and the clean stop,
# not the NULL guard behind them: on glibc fopen(NULL) and unlink(NULL) fail
# rather than crash, so this passes with the guard removed.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
# shellcheck source=tests/lib/xymond-daemon.sh
. "$(dirname "$0")/../lib/xymond-daemon.sh"

require_bin XYMOND xymond/xymond
require_bin XYMONCLIENT common/xymon
require_shm_segments "$(grep -c 'setup_channel(C_[A-Z_]*, CHAN_MASTER)' "$(find_root)/xymond/xymond.c")"
require_cfg XYMONSERVER_CFG xymond/etcfiles/xymonserver.cfg

work=$(mktempdir)
mkdir -p "$work/home/etc" "$work/home/tmp" "$work/home/www" "$work/run"
printf 'page test Test\n127.0.0.1 testhost.example.com # conn\n' > "$work/hosts.cfg"

# The generated configuration names the build's own XYMONRUNDIR; without that
# line the variable is whatever the environment gives each case below.
sed -e 's|^XYMONHOME=.*|XYMONHOME="'"$work"'/home"|' \
    -e 's|^XYMONTMP=.*|XYMONTMP="'"$work"'/home/tmp"|' \
    -e '/^XYMONRUNDIR=/d' \
	"$XYMONSERVER_CFG" > "$work/xymonserver.cfg"

xymond_launch() {
	local port=$1; shift
	"$XYMOND" --no-daemon --listen="127.0.0.1:$port" \
		--hosts="$work/hosts.cfg" --env="$work/xymonserver.cfg" \
		--checkpoint-file="$work/chk.out" \
		"$@" \
		> "$work/xymond.log" 2>&1 &
	XYMOND_PID=$!
}

# Stop the daemon and require an ordinary exit, not a signal or a crash.
stop_xymond() {
	local rc=0
	kill -TERM "$XYMOND_PID" 2>/dev/null || fail "xymond was not running to be stopped"
	wait "$XYMOND_PID" 2>/dev/null || rc=$?
	[ "$rc" -eq 0 ] || fail "$1: xymond exited with status $rc when stopped: $(cat "$work/xymond.log")"
}

# ---- the default pidfile is in XYMONRUNDIR -----------------------------------
export XYMONRUNDIR="$work/run"
start_xymond
register_cleanup "kill $XYMOND_PID 2>/dev/null || true"
i=0
while [ "$i" -lt 50 ] && [ ! -s "$work/run/xymond.pid" ]; do sleep 0.1; i=$((i + 1)); done
[ -s "$work/run/xymond.pid" ] || fail "xymond wrote no pidfile in XYMONRUNDIR: $(ls "$work/run") $(cat "$work/xymond.log")"
[ "$(cat "$work/run/xymond.pid")" = "$XYMOND_PID" ] \
	|| fail "$work/run/xymond.pid holds $(cat "$work/run/xymond.pid"), not xymond's pid $XYMOND_PID"
stop_xymond "default pidfile"
[ ! -e "$work/run/xymond.pid" ] || fail "xymond left its pidfile behind when it stopped"

# ---- a XYMONRUNDIR too long for the path -------------------------------------
export XYMONRUNDIR="$work/$(printf 'x%.0s' $(seq 1 5000))"
start_xymond
assert_contains "Default pidfile path does not fit under XYMONRUNDIR" "$(cat "$work/xymond.log")" \
	"an over-long XYMONRUNDIR is reported"
stop_xymond "over-long XYMONRUNDIR"

pass "xymond keeps its default pidfile in XYMONRUNDIR, and reports one too long for it"
