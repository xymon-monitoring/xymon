#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/xymond-rrd-rrdcached.sh
#
# xymond_rrd with RRDCACHED_ADDRESS naming a local rrdcached, and a graph of
# one host drawn while the newest updates are still in memory:
#
# - an update xymond_rrd takes is not on disk yet: it sits in xymond_rrd's
#   cache, and once handed over, in rrdcached's, which writes once an hour
#   here (-w 3600);
# - showgraph's page for the host brings the file up to date before it is
#   drawn: its flush request makes xymond_rrd hand its cache to rrdcached,
#   and rrd_graph() then has rrdcached write the file it reads.
#
# Each half fails alone: a flush request xymond_rrd reads too late leaves the
# update in its cache, and a graph drawn without RRDCACHED_ADDRESS leaves it
# in rrdcached's. Skipped where rrdcached is not installed.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
require_cc
if [ ! -f "$ROOT/include/config.h" ] || [ ! -f "$ROOT/web/showgraph.cgi" ]; then
	skip "tree not built with RRD support (run make first; the post-build CI suite covers this)"
fi
require_bin XYMOND_RRD xymond/xymond_rrd
require_bin SHOWGRAPH web/showgraph.cgi
RRDCACHED=$(command -v rrdcached 2>/dev/null) || skip "rrdcached is not installed"

work=$(mktempdir)
rrddef=$(xymon_makevar "$ROOT" RRDDEF)
rrdinc=$(xymon_makevar "$ROOT" RRDINCDIR)
rrdlibs=$(xymon_makevar "$ROOT" RRDLIBS)
[ -n "$rrdlibs" ] || rrdlibs="-lrrd"
# shellcheck disable=SC2086  # the flag lists are meant to split
"$CC" $(xymon_cflags "$ROOT") $rrddef $rrdinc -o "$work/last" \
	"$(dirname "$0")/xymond-rrd-rrdcached-last.c" $rrdlibs $(xymon_ldflags "$ROOT") 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; fail "the on-disk reader does not compile"; }

mkdir -p "$work/home/etc" "$work/tmp" "$work/run" "$work/rrd" "$work/journal"
: > "$work/home/etc/analysis.cfg"
: > "$work/hosts.cfg"
cp "$ROOT/xymond/etcfiles/graphs.cfg.DIST" "$work/graphs.cfg"

# A socket path is limited to sizeof(sun_path); the work directory may be
# longer than that, so the daemon's socket lives in a short one.
sockdir=$(mktemp -d "${TMPDIR:-/tmp}/xrc.XXXXXX")
register_cleanup "rm -rf '$sockdir'"
sock="$sockdir/s"
"$RRDCACHED" -g -l "unix:$sock" -b "$work/rrd" -w 3600 -f 7200 -z 1 \
	-j "$work/journal" -p "$work/rrdcached.pid" >"$work/rrdcached.log" 2>&1 &
daemon=$!
register_cleanup "stop_process $daemon"
for _ in $(seq 1 50); do
	[ -S "$sock" ] && break
	sleep 0.1
done
[ -S "$sock" ] || fail "rrdcached did not start: $(cat "$work/rrdcached.log")"

mkfifo "$work/feed"
RRDCACHED_ADDRESS="unix:$sock" \
XYMONHOME="$work/home" XYMONVAR="$work" XYMONTMP="$work/tmp" \
XYMONRUNDIR="$work/run" XYMONRRDS="$work/rrd" HOSTSCFG="!$work/hosts.cfg" \
TEST2RRD="inode" GRAPHS="inode" \
	"$XYMOND_RRD" --rrddir="$work/rrd" --debug \
	<"$work/feed" >"$work/rrd.log" 2>&1 &
worker=$!
register_cleanup "stop_process $worker"
exec 9>"$work/feed"

# status HOST TIME SEQ -- one inode status, timestamped TIME, as channel
# message SEQ: an RRD update. xymond_rrd drops a second update it sees under
# the same sequence number as a duplicate, so each message has its own.
status() {
	printf '@@status#%s|%s|127.0.0.1||%s|inode|%s|green||||||||||unix|\n' "$3" "$2" "$1" "$(($2 + 1800))" >&9
	printf 'status %s.inode green %s - Filesystems ok\n' "$1" "$2" >&9
	printf 'Filesystem itotal iused ifree %%%%iused Mounted on\n' >&9
	printf '/dev/ld0a 259070 41020 218050 15%%%% /\n@@\n' >&9
}

rrd="$work/rrd/unix.test/inode,root.rrd"
t0=$(date +%s)
status unix.test "$t0" 1
for _ in $(seq 1 100); do
	[ -f "$rrd" ] && break
	sleep 0.1
done
[ -f "$rrd" ] || fail "xymond_rrd created no RRD: $(cat "$work/rrd.log")"
t2=$((t0 + 120))
status unix.test $((t0 + 60)) 2
status unix.test "$t2" 3
sleep 1

ondisk() { env -u RRDCACHED_ADDRESS "$work/last" "$rrd"; }
before=$(ondisk) || fail "the RRD cannot be read: $before"
[ "$before" -lt "$t2" ] \
	|| fail "the newest update was on disk before any flush ($before, $t2): it bypassed both caches"

# xymond_rrd reads the control socket between messages, as it does when
# xymond keeps it busy: other hosts' statuses go on arriving while the page
# is drawn.
( for i in $(seq 1 40); do status "wake$i.test" "$t0" $((10 + i)); sleep 0.05; done ) &
waker=$!
page=$(env -i PATH="$PATH" REQUEST_METHOD=GET RRDCACHED_ADDRESS="unix:$sock" \
	QUERY_STRING="host=unix.test&service=inode&graph=hourly&action=view" \
	XYMONHOME="$work" XYMONRUNDIR="$work/run" XYMONRRDS="$work/rrd" \
	"$SHOWGRAPH" --config="$work/graphs.cfg" 2>&1 | tr -d '\000' || true)
wait "$waker" || true
exec 9>&-

assert_contains "Content-type: image/png" "$page" "the page for one host is drawn"
after=$(ondisk) || fail "the RRD cannot be read after the graph: $after"
[ "$after" = "$t2" ] \
	|| fail "the graph of one host was drawn from a file not brought up to date: its last update is $after on disk, not $t2 (xymond_rrd log: $(tail -20 "$work/rrd.log"))"

pass "xymond_rrd sends its updates to rrdcached, and a graph of one host brings the file up to date before drawing it"
