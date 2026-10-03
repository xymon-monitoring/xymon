#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/xymond-history-pidfile.sh
#
# xymond_history writes its pidfile to XYMONRUNDIR, and trimhistory reads it
# from there to send the daemon a HUP after trimming allevents. Before, the
# daemon's default was the log directory while trimhistory already looked in
# XYMONRUNDIR, and an empty XYMONRUNDIR sent trimhistory to "/xymond_history.pid".
# Both now resolve the directory through xymon_rundir(), and both paths are
# bounded: --pidfile= was copied with strcpy() into a PATH_MAX buffer.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin XYMOND_HISTORY "xymond/xymond_history"
require_bin TRIMHISTORY "xymond/trimhistory"

work=$(mktempdir); register_cleanup "rm -rf '$work'"
mkdir -p "$work/hist" "$work/histlogs" "$work/logs" "$work/run" "$work/custom"
: >"$work/hosts.cfg"

# Run a command in the daemon's environment; the first argument is
# XYMONRUNDIR, "-" for unset.
histenv() {
	local rundir=$1
	shift
	[ "$rundir" = "-" ] || set -- "XYMONRUNDIR=$rundir" "$@"
	# Always run in a subshell -- a pipeline or $(...) -- so exec makes $!
	# the daemon itself, not a shell around it.
	# HOSTSCFG starts with "!": trimhistory reads the file instead of asking
	# whatever xymond runs on this host.
	exec env -i PATH="$PATH" XYMONHOME="$work" XYMONSERVERLOGS="$work/logs" \
		XYMONHISTDIR="$work/hist" XYMONHISTLOGS="$work/histlogs" \
		HOSTSCFG="!$work/hosts.cfg" "$@"
}

# Start the daemon on an open pipe, wait for the pidfile it should write, and
# stop it again. Fails naming where the file was expected. Killing the daemon
# leaves the sleep running out its 30 seconds, so it must not hold the test's
# stderr: whoever reads this test's output would wait for it.
expect_pidfile() {
	local rundir=$1 want=$2 what=$3 pid
	shift 3
	rm -f "$want"
	sleep 30 2>/dev/null | histenv "$rundir" "$XYMOND_HISTORY" "$@" >"$work/history.log" 2>&1 &
	pid=$!
	register_cleanup "kill $pid 2>/dev/null || true"
	for _ in $(seq 1 100); do
		[ -s "$want" ] && break
		sleep 0.1
	done
	kill "$pid" 2>/dev/null || true
	[ -s "$want" ] || fail "$what: no pidfile at $want (logs: $(ls "$work/logs"); run: $(ls "$work/run")): $(cat "$work/history.log")"
	[ "$(cat "$want")" = "$pid" ] || fail "$what: $want holds $(cat "$want"), not the daemon's pid $pid"
	rm -f "$want"
}

expect_pidfile "$work/run" "$work/run/xymond_history.pid" "default pidfile with XYMONRUNDIR set"
expect_pidfile "" "$work/logs/xymond_history.pid" "default pidfile with XYMONRUNDIR empty"
expect_pidfile - "$work/logs/xymond_history.pid" "default pidfile with XYMONRUNDIR unset"
expect_pidfile "$work/run" "$work/custom/h.pid" "--pidfile=" --pidfile="$work/custom/h.pid"

# A --pidfile= longer than PATH_MAX is refused with a message, and the daemon
# still runs to the end of its input rather than overflowing the buffer.
long="$work/$(printf 'x%.0s' $(seq 1 5000))"
rc=0
out=$(histenv "$work/run" "$XYMOND_HISTORY" --pidfile="$long" </dev/null 2>&1) || rc=$?
[ "$rc" -eq 0 ] || fail "xymond_history with an overlong --pidfile= exited $rc: $out"
assert_contains "--pidfile= path does not fit" "$out" "overlong --pidfile= refused"

# and so is a default built from an XYMONRUNDIR that long.
rc=0
out=$(histenv "$long" "$XYMOND_HISTORY" </dev/null 2>&1) || rc=$?
[ "$rc" -eq 0 ] || fail "xymond_history with an overlong XYMONRUNDIR exited $rc: $out"
assert_contains "XYMONRUNDIR is too long for the default pidfile path; no pidfile unless --pidfile= is given" \
	"$out" "overlong default pidfile refused"
# The default is worked out before the options are read, so the message comes
# even when --pidfile= follows -- and then that pidfile is written.
expect_pidfile "$long" "$work/custom/h2.pid" "--pidfile= with an overlong XYMONRUNDIR" --pidfile="$work/custom/h2.pid"

# ---- trimhistory signals the daemon through the same file -------------------
# A stand-in for xymond_history that records the HUP; its pid is written where
# the daemon would write it, and trimhistory must find it there.
: >"$work/hist/allevents"
expect_hup() {
	local rundir=$1 pidfile=$2 what=$3 pid
	rm -f "$work/got-hup" "$work/trap-set"
	# The pid is handed out only once the trap is set: a HUP arriving before
	# it would kill the stand-in, and the test would blame trimhistory.
	sh -c 'trap "touch \"$0\"; exit 0" HUP; touch "$1"; while :; do sleep 1; done' \
		"$work/got-hup" "$work/trap-set" &
	pid=$!
	register_cleanup "kill $pid 2>/dev/null || true"
	for _ in $(seq 1 50); do
		[ -f "$work/trap-set" ] && break
		sleep 0.1
	done
	[ -f "$work/trap-set" ] || fail "$what: the stand-in never set its HUP trap"
	echo "$pid" >"$pidfile"
	out=$(histenv "$rundir" "$TRIMHISTORY" --cutoff=1 2>&1) \
		|| fail "$what: trimhistory failed: $out"
	for _ in $(seq 1 50); do
		[ -f "$work/got-hup" ] && break
		sleep 0.1
	done
	kill "$pid" 2>/dev/null || true
	rm -f "$pidfile"
	[ -f "$work/got-hup" ] || fail "$what: trimhistory did not signal the pid in $pidfile: $out"
}

expect_hup "$work/run" "$work/run/xymond_history.pid" "trimhistory with XYMONRUNDIR set"
expect_hup "" "$work/logs/xymond_history.pid" "trimhistory with XYMONRUNDIR empty"
expect_hup - "$work/logs/xymond_history.pid" "trimhistory with XYMONRUNDIR unset"

# An XYMONRUNDIR too long for the pidfile path is refused with a message, not
# overflowed; trimming still completes.
out=$(histenv "$long" "$TRIMHISTORY" --cutoff=1 2>&1) \
	|| fail "trimhistory with an overlong XYMONRUNDIR failed: $out"
assert_contains "XYMONRUNDIR is too long for a pidfile path" "$out" "trimhistory refuses an overlong pidfile path"

pass "xymond_history and trimhistory agree on the pidfile in XYMONRUNDIR, and bound its path"
