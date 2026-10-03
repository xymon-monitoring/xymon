#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/rrd/rrdcached-relative-path-key.sh
#
# showgraph's nostale filter asks rrd_last() how fresh an RRD is, and passes
# the filename as "./<name>" -- the "./" keeps a "-"-prefixed name out of
# option parsing while staying a plain operand for a legacy rrd_last() that
# opens argv[1] directly (web/showgraph.c). Every rrd_* call honours
# RRDCACHED_ADDRESS, and rrdcached keys its pending-update cache by the
# filename string it is handed. So the "./" prefix is only safe if the daemon
# resolves "./name" and the bare "name" to the *same* cache entry: otherwise a
# reading still sitting in the daemon would not be flushed for the probe, and
# an RRD kept fresh only in the cache could read as stale.
#
# This drives rrdcached directly (no Xymon binary): queue an update under the
# bare name so it sits in the cache unwritten, then read the last-update time
# back through the daemon as "./name". If "./name" keyed a different entry the
# flush would be a no-op and the read would return the stale on-disk time; it
# returns the freshly cached one, so the two forms are the same key.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

command -v rrdtool   >/dev/null 2>&1 || skip "rrdtool not in PATH"
command -v rrdcached >/dev/null 2>&1 || skip "rrdcached not in PATH (optional rrdtool daemon)"

work=$(mktempdir)
# rrdcached refuses a base directory reached through a symlink (e.g. macOS
# /tmp -> /private/tmp, /var -> /private/var), so hand it the resolved path.
base=$(cd "$work" && pwd -P)
sock="$base/rc.sock"
pidf="$base/rc.pid"

rrdcached -l "unix:$sock" -b "$base" -B -w 3600 -f 7200 -p "$pidf" -m 0700 \
	2>"$base/rc.err" || { cat "$base/rc.err" >&2; skip "rrdcached would not start here"; }
register_cleanup "[ -f '$pidf' ] && kill \"\$(cat '$pidf')\" 2>/dev/null || true"

# Wait for the socket rather than sleeping a fixed time.
for _ in 1 2 3 4 5 6 7 8 9 10; do [ -S "$sock" ] && break; sleep 0.2; done
[ -S "$sock" ] || { cat "$base/rc.err" >&2; skip "rrdcached socket never appeared"; }

daemon="unix:$sock"
now=$(date +%s)
start=$((now - 600))

cd "$base"
rrdtool create test.rrd --start "$start" --step 300 \
	DS:x:GAUGE:600:U:U RRA:LAST:0.5:1:10

pre=$(rrdtool last test.rrd)   # on-disk last-update, before any update

# Queue an update under the BARE name. With -w 3600 it stays in the daemon's
# cache, unwritten to disk.
rrdtool update --daemon "$daemon" test.rrd "$now:42"

# The file on disk must still show the old time -- proves the update is
# cache-only, so a later "last" that returns the new time can only have got
# it by flushing the cached entry.
ondisk=$(rrdtool last test.rrd)
assert_equal "$pre" "$ondisk" \
	"queued update was written to disk immediately -- test cannot distinguish the cache"

# THE ASSERTION: read last-update through the daemon as "./test.rrd", before
# any bare-name read has had a chance to flush. It must reflect the queued
# update (newer than on disk), which is only possible if "./test.rrd" keys the
# same cache entry the bare name was queued under.
dot=$(rrdtool last --daemon "$daemon" ./test.rrd)
[ "$dot" != "$pre" ] || fail \
	"rrdcached did not flush the queued update for './test.rrd' (last=$dot, on-disk=$pre) -- './name' keys a different cache entry than the bare name"

# And the bare name agrees (now reading the just-flushed value from disk).
bare=$(rrdtool last --daemon "$daemon" test.rrd)
assert_equal "$dot" "$bare" \
	"'./test.rrd' and 'test.rrd' disagree through the daemon"

pass "rrdcached keys './name' and 'name' to the same cache entry"
