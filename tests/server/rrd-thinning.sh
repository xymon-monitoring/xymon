#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/rrd-thinning.sh
#
# Write-thinning: a sample carrying no new information is not written.
# The gate is the block's own DS heartbeat declaration (keepalive =
# half the smallest declared heartbeat; the stock 600 = 2*step declares
# no tolerance and thins nothing). On a change after a gap the writer
# pre-writes the old values one step earlier, pinning the step edge -
# without it rrdtool back-fills the whole gap with the NEW value.
# ABSOLUTE DSes and 'U' values are excluded: equal ABSOLUTE readings
# are new information, and thinning unknowns would invent continuity.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin XYMOND_RRD "xymond/xymond_rrd"
command -v rrdtool >/dev/null 2>&1 || skip "rrdtool CLI not available"

work=$(mktempdir)
mkdir -p "$work/rrd" "$work/tmp"

# Step-aligned base; later samples run FORWARD from here (rrdtool accepts
# future timestamps, but rejects ones behind a file's creation time).
ts=$(( $(date +%s) / 300 * 300 + 300 ))	# next step boundary: never behind the file's create time

msg_at() {  # msg_at <statusts> <blockname> <dsspec> <value> -- one message on stdout
	printf '@@status|%s|127.0.0.1|origin|testhost|diskio|%s|green||green|%s|0||0||%s|0|linux|/\n' \
		"$1" $(($1+1800)) "$ts" "$ts"
	printf '<!--XYMON METRICS: %s\n%s\nx %s\n-->\ns\n@@\n' "$2" "$3" "$4"
}
run_daemon() {  # consume stdin messages in ONE daemon process (thinning state is per process)
	env XYMONHOME="$work" XYMONTMP="$work/tmp" \
		"$XYMOND_RRD" --rrddir="$work/rrd" --no-cache 2>/dev/null
}
lastupd() {  # lastupd <rrdfile> -> last update timestamp
	rrdtool lastupdate "$work/rrd/testhost/$1" | awk '/^[0-9]+:/{sub(":","",$1); print $1}'
}

# One daemon run, five samples of the declared-tolerance block:
# heartbeat 7200 -> keepalive 3600. Unchanged samples inside the window
# skip; the unchanged sample past the keepalive writes; the change after
# a gap writes AND pre-writes the old value one step earlier.
{
	msg_at "$ts"          thn "DS:v:GAUGE:7200:0:U" 5
	msg_at $((ts+300))    thn "DS:v:GAUGE:7200:0:U" 5
	msg_at $((ts+600))    thn "DS:v:GAUGE:7200:0:U" 5
} | run_daemon
[ "$(lastupd thn.x.rrd)" = "$ts" ] \
	|| fail "unchanged samples inside the keepalive were written (last=$(lastupd thn.x.rrd), want $ts)"

{
	msg_at "$ts"          thn "DS:v:GAUGE:7200:0:U" 5
	msg_at $((ts+3900))   thn "DS:v:GAUGE:7200:0:U" 5
	msg_at $((ts+5100))   thn "DS:v:GAUGE:7200:0:U" 9
} | run_daemon
[ "$(lastupd thn.x.rrd)" = "$((ts+5100))" ] || fail "changed sample was not written"
fetchval() {  # fetchval <endts> -> consolidated value of the bucket ending there
	rrdtool fetch "$work/rrd/testhost/thn.x.rrd" AVERAGE -s $(($1-300)) -e "$1" \
		| awk -v t="$1" '$1 == t":" {print $2}'
}
echo "$(fetchval $((ts+4800)))" | grep -q '^5' \
	|| fail "gap not pinned at the old value: bucket $(fetchval $((ts+4800)))"
echo "$(fetchval $((ts+5100)))" | grep -q '^9' \
	|| fail "change bucket does not carry the new value: $(fetchval $((ts+5100)))"

# The stock heartbeat (600 = 2*step) declares no tolerance: no thinning,
# every sample writes - existing producers see zero behavior change.
{
	msg_at "$ts"          std "DS:v:GAUGE:600:0:U" 5
	msg_at $((ts+300))    std "DS:v:GAUGE:600:0:U" 5
} | run_daemon
[ "$(lastupd std.x.rrd)" = "$((ts+300))" ] \
	|| fail "stock-heartbeat block was thinned (last=$(lastupd std.x.rrd))"

# ABSOLUTE is excluded even with a large heartbeat: equal readings are
# new information (the value resets on read).
{
	msg_at "$ts"          abs "DS:v:ABSOLUTE:7200:0:U" 5
	msg_at $((ts+300))    abs "DS:v:ABSOLUTE:7200:0:U" 5
} | run_daemon
[ "$(lastupd abs.x.rrd)" = "$((ts+300))" ] \
	|| fail "ABSOLUTE block was thinned (last=$(lastupd abs.x.rrd))"

# 'U' always writes: unknown is not a constant to interpolate.
{
	msg_at "$ts"          unk "DS:v:GAUGE:7200:0:U" U
	msg_at $((ts+300))    unk "DS:v:GAUGE:7200:0:U" U
} | run_daemon
[ "$(lastupd unk.x.rrd)" = "$((ts+300))" ] \
	|| fail "unknown values were thinned (last=$(lastupd unk.x.rrd))"

echo "OK $(basename "$0")"
