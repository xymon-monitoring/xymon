#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/xymond-rrd-flush-quiet.sh
#
# A cache-flush request ("/host/" on an rrdctl.* socket, as showgraph sends
# it before drawing a graph) is served while the channel is quiet, not when
# the next message comes. xymond_rrd waited for a channel message with no
# bound, and read its control socket only between two: on the data channel
# that is the next client report, minutes away, so a graph drawn 0.3 s after
# its flush read the file a cache behind -- up to an hour of updates.
#
# Here an update for unix.test is cached, the channel then kept open and
# silent, and the flush sent: the cached update must be written within a few
# seconds, the worker still running.
#
# A flush that finds nothing to write must not start the 60 s that must pass
# between two flushes of a host either. Every twelfth update xymond_rrd
# takes is written through, the cache with it (do_rrd.c, CACHESZ, its count
# taking the first update too): a flush right after finds the cache empty;
# one sent after the next update, a second later, is served. Another host,
# two.test, so that the flush above, which did write, has started no 60 s
# for it: its first flush is the one that finds nothing.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
require_cc
require_bin XYMOND_RRD xymond/xymond_rrd

work=$(mktempdir)
"$CC" -o "$work/sender" "$(dirname "$0")/xymond-rrd-ctlmsg-sender.c" 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; fail "the sender does not compile"; }

mkdir -p "$work/home/etc" "$work/tmp" "$work/run" "$work/rrd"
: > "$work/home/etc/analysis.cfg"
: > "$work/hosts.cfg"
mkfifo "$work/feed"

XYMONHOME="$work/home" XYMONVAR="$work" XYMONTMP="$work/tmp" \
XYMONRUNDIR="$work/run" XYMONRRDS="$work/rrd" HOSTSCFG="!$work/hosts.cfg" \
TEST2RRD="inode" GRAPHS="inode" \
	"$XYMOND_RRD" --rrddir="$work/rrd" --debug \
	<"$work/feed" >"$work/rrd.log" 2>&1 &
worker=$!
register_cleanup "stop_process $worker"
exec 9>"$work/feed"
register_cleanup "exec 9>&- 2>/dev/null || true"

base=$(date +%s)
seq=0
# status N [HOST] -- an inode status for HOST, unix.test by default, N
# minutes on, with the channel's next sequence number: without one, an
# update is a duplicate.
status() {
	local ts=$(( base + $1 * 60 )) host=${2:-unix.test}
	seq=$((seq + 1))
	printf '@@status#%d/%s|%s|127.0.0.1||%s|inode|%s|green||||||||||unix|\n' "$seq" "$host" "$ts" "$host" "$((ts + 1800))" >&9
	printf 'status %s.inode green %s - Filesystems ok\n' "$(printf '%s' "$host" | tr . ,)" "$ts" >&9
	printf 'Filesystem itotal iused ifree %%%%iused Mounted on\n' >&9
	printf '/dev/ld0a 259070 41020 218050 15%%%% /\n@@\n' >&9
}
# lines PATTERN -- how many lines of the log hold PATTERN.
lines() { grep -c -- "$1" "$work/rrd.log" || true; }
# waitfor PATTERN COUNT -- until the log holds PATTERN COUNT times, at most
# 10 s: true when it does.
waitfor() {
	local _
	for _ in $(seq 1 100); do
		[ "$(lines "$1")" -ge "$2" ] && return 0
		sleep 0.1
	done
	return 1
}

status 0
sock=
for _ in $(seq 1 100); do
	for f in "$work"/run/rrdctl.*; do
		[ -S "$f" ] && sock=${f##*/}
	done
	[ -n "$sock" ] && [ -n "$(ls "$work/rrd/unix.test" 2>/dev/null)" ] && break
	sleep 0.1
done
[ -n "$sock" ] || fail "xymond_rrd made no control socket: $(cat "$work/rrd.log")"

# The channel stays open and says nothing; the first update is cached.
sleep 1

# A flush on the quiet channel is served.
before=$(lines "Flushing '")
"$work/sender" "$work/run/$sock" "/unix.test/" || fail "could not send the flush"
waitfor "Flushing '" $((before + 1)) \
	|| fail "a flush sent while the channel was quiet was not served: $(grep -v 'Want msg' "$work/rrd.log" | tail -n 20)"
kill -0 "$worker" 2>/dev/null || fail "xymond_rrd stopped: $(grep -v 'Want msg' "$work/rrd.log" | tail -n 20)"

# More than 60 s are not waited here: the throttle is reached from a cache
# that empties by itself. Eleven updates more, the twelfth taken, written
# through with what is cached: the cache is empty.
written=$(lines "updates pending")
for i in $(seq 1 11); do status "$i" two.test; done
waitfor "updates pending" $((written + 1)) \
	|| fail "the twelfth update was not written through: $(grep -v 'Want msg' "$work/rrd.log" | tail -n 20)"
sleep 1
skipped=$(lines "skipped, too soon")
"$work/sender" "$work/run/$sock" "/two.test/" || fail "could not send the flush that finds nothing"
sleep 1
status 12 two.test
sleep 1
written=$(lines "updates pending")
"$work/sender" "$work/run/$sock" "/two.test/" || fail "could not send the flush after it"
waitfor "updates pending" $((written + 1)) \
	|| fail "a flush after one that found nothing to write was held back: $(grep -v 'Want msg' "$work/rrd.log" | tail -n 12)"
assert_equal "$skipped" "$(lines "skipped, too soon")" "and none was skipped as too soon"

exec 9>&-
wait "$worker" 2>/dev/null || true

pass "xymond_rrd serves a cache flush while its channel is quiet, and a flush that wrote nothing holds no other back"
