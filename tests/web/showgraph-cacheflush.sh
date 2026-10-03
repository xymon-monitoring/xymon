#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/web/showgraph-cacheflush.sh
#
# Before drawing a graph, showgraph asks xymond_rrd to flush the host's cached
# updates, sending a datagram to each rrdctl.* socket in XYMONRUNDIR -- or in
# XYMONSERVERLOGS when XYMONRUNDIR is empty, where it used to open "" and
# flush nothing. Each socket path is bounded: a long entry overflowed sun_path.
# A receiver stands in for xymond_rrd's socket and prints what arrives.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
require_cc
require_bin SHOWGRAPH "web/showgraph.cgi"

work=$(mktempdir); register_cleanup "rm -rf '$work'"
"$CC" -o "$work/receiver" "$(dirname "$0")/showgraph-cacheflush-receiver.c" 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; fail "receiver does not compile"; }

mkdir -p "$work/rrd/testhost" "$work/run" "$work/logs"
touch "$work/rrd/testhost/tcp.conn.rrd"
cp "$ROOT/xymond/etcfiles/graphs.cfg.DIST" "$work/graphs.cfg"

# One graph request for testhost; the first argument is XYMONRUNDIR, "-" for
# unset. rrd_graph fails on the stub file, but the flush comes before it.
render() {
	local rundir=$1
	set -- "XYMONSERVERLOGS=$work/logs"
	[ "$rundir" = "-" ] || set -- "$@" "XYMONRUNDIR=$rundir"
	env -i PATH="$PATH" REQUEST_METHOD=GET \
		QUERY_STRING="host=testhost&service=tcp:conn&graph=hourly&action=view" \
		XYMONHOME="$work" "$@" \
		"$SHOWGRAPH" --config="$work/graphs.cfg" --rrddir="$work/rrd/testhost" 2>&1 || true
}

# Bind a receiver at SOCKET, render with RUNDIR into render.log, and require
# the flush request for testhost to arrive.
expect_flush() {
	local rundir=$1 sock=$2 what=$3 rpid
	"$work/receiver" "$sock" >"$work/received" 2>&1 &
	rpid=$!
	register_cleanup "kill $rpid 2>/dev/null || true"
	for _ in $(seq 1 50); do
		[ -S "$sock" ] && break
		sleep 0.1
	done
	[ -S "$sock" ] || fail "$what: the receiver did not bind $sock: $(cat "$work/received")"
	render "$rundir" >"$work/render.log"
	wait "$rpid" || fail "$what: no flush request reached $sock: $(cat "$work/render.log")"
	assert_contains "/testhost/" "$(cat "$work/received")" "$what: flush request for testhost"
}

expect_flush "$work/run" "$work/run/rrdctl.1" "XYMONRUNDIR set"
expect_flush "" "$work/logs/rrdctl.1" "XYMONRUNDIR empty"
expect_flush - "$work/logs/rrdctl.1" "XYMONRUNDIR unset"

# An entry whose path is too long for sun_path is skipped with a message, and
# the sockets beside it are still flushed.
long="rrdctl.$(printf 'x%.0s' $(seq 1 120))"
touch "$work/run/$long"
expect_flush "$work/run" "$work/run/rrdctl.2" "beside an overlong entry"
assert_contains "rrdctl socket path too long, skipping $work/run/$long" "$(cat "$work/render.log")" \
	"the overlong entry is named"
rm -f "$work/run/$long"

# At the boundary: a path of exactly sizeof(sun_path) has no room for the
# terminator and is skipped; one byte shorter is used.
"$CC" -o "$work/sun-path-size" "$(dirname "$0")/../lib/sun-path-size.c" 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; fail "sun-path-size does not compile"; }
size=$("$work/sun-path-size")
mkdir -p "$work/b"
# "$work/b/" + "rrdctl." + m y's
m=$((size - ${#work} - 3 - 7))
if [ "$m" -gt 1 ]; then
	at="rrdctl.$(printf 'y%.0s' $(seq 1 "$m"))"
	below="rrdctl.$(printf 'z%.0s' $(seq 1 $((m - 1))))"
	touch "$work/b/$at" "$work/b/$below"
	out=$(render "$work/b")
	assert_contains "skipping $work/b/$at" "$out" "an entry whose path is exactly sizeof(sun_path) ($size) is skipped"
	assert_not_contains "skipping $work/b/$below" "$out" "an entry one byte under sizeof(sun_path) is used"
else
	# The work directory alone is too long for a path of that size here.
	printf '  note: boundary not checked, %s is too long for a %s-character path\n' "$work" "$size" >&2
fi

# A directory showgraph cannot open is named in its error.
out=$(render "$work/absent")
assert_contains "Cannot access XYMONRUNDIR ($work/absent)" "$out" "a missing XYMONRUNDIR is named"

pass "showgraph flushes the rrdctl sockets in XYMONRUNDIR, and bounds each path at sizeof(sun_path)"
