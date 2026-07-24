#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/xymond-rrd-markers.sh
#
# Self-describing statuses: a status message carrying an embedded
# "<!--XYMON METRICS: <name>" block (or the legacy "<!--DEVMON RRD:"
# banner) is routed to the RRD block writer by content, with no TEST2RRD
# mapping. Feed real messages to the built xymond_rrd over stdin and
# assert which RRD files get created.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin XYMOND_RRD "xymond/xymond_rrd"

work=$(mktempdir)

# METRICS blocks create files eagerly on the first sample. Unknown
# banner attributes are ignored (the dialect's generic forward
# compatibility) - a section below pins that.

feed_status() {  # feed_status <testname> <bodyfile> -- send one status message
	local ts; ts=$(date +%s)
	rm -rf "$work/rrd"; mkdir -p "$work/rrd" "$work/tmp"
	{
		printf '@@status|%s|127.0.0.1|origin|testhost|%s|%s|green||green|%s|0||0||%s|0|linux|/\n' \
			"$ts" "$1" $((ts+1800)) "$ts" "$ts"
		cat "$2"
		printf '@@\n'
	} | env XYMONHOME="$work" XYMONTMP="$work/tmp" \
		"$XYMOND_RRD" --rrddir="$work/rrd" --no-cache 2>/dev/null
	ls "$work/rrd/testhost" 2>/dev/null || true
}

# A status with two METRICS blocks: files appear for every instance of both,
# even though "diskio" has no TEST2RRD mapping.
cat >"$work/body-metrics" <<'EOF'
<!--XYMON METRICS: diskio_ops
DS:reads:GAUGE:600:0:U DS:writes:GAUGE:600:0:U
ada0 10:20
ada1 5:6
-->
<!--XYMON METRICS: diskio_busy
DS:busy:GAUGE:600:0:100
ada0 5
ada1 10
-->
<!--XYMON GRAPH: diskio_ops -->

Disk I/O Status

ada0: 10 r/s, 20 w/s
ada1: 5 r/s, 6 w/s
EOF
out=$(feed_status diskio "$work/body-metrics")
assert_contains "diskio_ops.ada0.rrd" "$out" "METRICS block creates one file per instance"
assert_contains "diskio_ops.ada1.rrd" "$out" "METRICS block creates one file per instance"
assert_contains "diskio_busy.ada0.rrd" "$out" "second METRICS block in the same message written too"
assert_contains "diskio_busy.ada1.rrd" "$out" "second METRICS block in the same message written too"

# An UNNAMED "<!--XYMON METRICS" block defaults its RRD prefix to the test name
# (name-optional: one homogeneous block per test).
cat >"$work/body-unnamed" <<'EOF'
<!--XYMON METRICS
DS:temp:GAUGE:600:0:U
sda 32
nvme0 29
-->

Disk temperatures
EOF
out=$(feed_status smarttemp "$work/body-unnamed")
assert_contains "smarttemp.sda.rrd" "$out" "unnamed METRICS block defaults its RRD prefix to the test name"
assert_contains "smarttemp.nvme0.rrd" "$out" "unnamed METRICS block: every instance uses the test name"

# The bare-colon spelling with no name behaves identically.
cat >"$work/body-unnamed2" <<'EOF'
<!--XYMON METRICS:
DS:temp:GAUGE:600:0:U
sdb 30
-->
EOF
out=$(feed_status smarttemp "$work/body-unnamed2")
assert_contains "smarttemp.sdb.rrd" "$out" "unnamed METRICS with a bare colon also defaults to the test name"

# A METRICS instance is reversibly encoded (rrdinstance_encode): a mount
# point or a name containing a comma round-trips to one unambiguous file,
# instead of the legacy lossy '/'->',' that aliased "/a/b" and "/a,b".
cat >"$work/body-encode" <<'EOF'
<!--XYMON METRICS: diskpath
DS:v:GAUGE:600:0:U
/data 1
/a,b 2
-->
status text
EOF
out=$(feed_status diskio "$work/body-encode")
assert_contains "diskpath.%2Fdata.rrd" "$out" "METRICS instance '/data' is percent-encoded, not ',data'"
assert_contains "diskpath.%2Fa,b.rrd" "$out" "'/a,b' stays distinct from what '/a/b' would encode to"

# An instance name with SPACES (a mount point / folder like "/media/My Book"):
# the value token is the last whitespace field, so the instance keeps its
# spaces (encoded to %20) instead of being split off at the first space.
cat >"$work/body-space" <<'EOF'
<!--XYMON METRICS: diskpath
DS:v:GAUGE:600:0:U
/media/My Book 34
plain 7
-->
status text
EOF
out=$(feed_status diskio "$work/body-space")
assert_contains "diskpath.%2Fmedia%2FMy%20Book.rrd" "$out" "space-bearing instance is kept whole and encoded, not split at the first space"
assert_not_contains "diskpath.%2Fmedia%2FMy.rrd" "$out" "instance must not be truncated at the first space"
assert_contains "diskpath.plain.rrd" "$out" "a plain single-word instance still writes normally"

# Block names become RRD filename prefixes, so invalid names skip the whole
# block - but a valid block later in the same message is still written.
cat >"$work/body-evil" <<'EOF'
<!--XYMON METRICS: ../evil
DS:v:GAUGE:600:0:U
oops 1
-->
<!--XYMON METRICS: good_one
DS:v:GAUGE:600:0:U
inst 1
-->
status text
EOF
out=$(feed_status diskio "$work/body-evil")
assert_not_contains "evil" "$out" "invalid block name is rejected"
assert_contains "good_one.inst.rrd" "$out" "valid block after a rejected one is still written"

# The legacy devmon banner is routed by content too (previously it needed
# TEST2RRD="<column>=devmon").
cat >"$work/body-devmon" <<'EOF'
<!--DEVMON RRD: if_load 0 0
DS:ds0:COUNTER:600:0:U DS:ds1:COUNTER:600:0:U
eth0.0 4678222:9966777
eth1.0 123:456
-->
status text
EOF
out=$(feed_status devtest "$work/body-devmon")
assert_contains "if_load.eth0.0.rrd" "$out" "legacy DEVMON RRD banner routed without TEST2RRD"
assert_contains "if_load.eth1.0.rrd" "$out" "legacy DEVMON RRD banner routed without TEST2RRD"

# The legacy banner's name becomes a filename prefix too: path separators
# must never escape the host's RRD directory.
cat >"$work/body-traversal" <<'EOF'
<!--DEVMON RRD: ../../escape 0 0
DS:v:GAUGE:600:0:U
oops 1
-->
status text
EOF
out=$(feed_status devtest "$work/body-traversal")
[ -e "$work/rrd/escape.oops.rrd" ] || [ -e "$work/escape.oops.rrd" ] \
	&& fail "path traversal: RRD file created outside the host directory"
[ -f "$work/rrd/testhost/..,..,escape.oops.rrd" ] \
	|| fail "legacy banner name is sanitized, not honored as a path"

# CRLF messages work: trailing CRs are stripped from banner names and
# value lines instead of poisoning filenames and RRD updates.
printf '<!--XYMON METRICS: crlf_metric\r\nDS:v:GAUGE:600:0:U\r\ninst 7\r\n-->\r\nstatus text\r\n' >"$work/body-crlf"
out=$(feed_status diskio "$work/body-crlf")
assert_contains "crlf_metric.inst.rrd" "$out" "CRLF message still creates clean RRD files"

# A value longer than the writer's assembly buffer is skipped, not
# overflowed - and the rest of the block is still written.
{
	printf '<!--XYMON METRICS: longline\n'
	printf 'DS:v:GAUGE:600:0:U\n'
	printf 'huge %s\n' "$(printf '9%.0s' $(seq 1 30000))"
	printf 'ok 1\n'
	printf -- '-->\n'
	printf 'status text\n'
} >"$work/body-long"
out=$(feed_status diskio "$work/body-long")
assert_not_contains "longline.huge.rrd" "$out" "oversized value line is skipped"
assert_contains "longline.ok.rrd" "$out" "lines after an oversized one are still written"

# Unknown banner attributes are ignored (forward compatibility): a
# block carrying one still creates every instance's file normally.
ts=$(date +%s)
rm -rf "$work/rrd"; mkdir -p "$work/rrd" "$work/tmp"
{
	printf '@@status|%s|127.0.0.1|origin|testhost|diskio|%s|green||green|%s|0||0||%s|0|linux|/\n' \
		"$ts" $((ts+1800)) "$ts" "$ts"
	printf '<!--XYMON METRICS: lazydemo futureattr=on\n'
	printf 'DS:r:GAUGE:600:0:U DS:w:GAUGE:600:0:U\n'
	printf 'idle 0:0\n'
	printf 'live 5:0\n'
	printf -- '-->\n'
	printf 'status text\n'
	printf '@@\n'
} | env XYMONHOME="$work" XYMONTMP="$work/tmp" \
	"$XYMOND_RRD" --rrddir="$work/rrd" --no-cache 2>/dev/null
[ -f "$work/rrd/testhost/lazydemo.live.rrd" ] \
	|| fail "unknown banner attribute: first sample must create the file"
[ -f "$work/rrd/testhost/lazydemo.idle.rrd" ] \
	|| fail "unknown banner attribute: a steady instance gets its file too"

mkdir -p "$work/etc"
cat >"$work/etc/graphs.cfg" <<'GDEFS'
[filt]
	EXSTOREPATTERN bad
[only]
	STOREPATTERN keep
[flz]
	STOREPATTERN pinned
[cfmax]
	FNPATTERN ^cfx\..+\.rrd
	DEF:m=x.rrd:v:MAX
GDEFS
feed2() {  # feed2 <blockheader> <inst1 val1a val1b> <inst2 val2a val2b>
	local ts; ts=$(date +%s)
	rm -rf "$work/rrd"; mkdir -p "$work/rrd" "$work/tmp"
	{
		printf '@@status|%s|127.0.0.1|origin|testhost|diskio|%s|green||green|%s|0||0||%s|0|linux|/\n' \
			"$ts" $((ts+1800)) "$ts" "$ts"
		printf '<!--XYMON METRICS: %s\nDS:v:GAUGE:600:0:U\n%s %s\n%s %s\n-->\nstatus\n@@\n' \
			"$1" "$2" "$3" "$5" "$6"
		printf '@@status|%s|127.0.0.1|origin|testhost|diskio|%s|green||green|%s|0||0||%s|0|linux|/\n' \
			$((ts+300)) $((ts+2100)) "$ts" "$ts"
		printf '<!--XYMON METRICS: %s\nDS:v:GAUGE:600:0:U\n%s %s\n%s %s\n-->\nstatus\n@@\n' \
			"$1" "$2" "$4" "$5" "$7"
	} | env XYMONHOME="$work" XYMONTMP="$work/tmp" \
		"$XYMOND_RRD" --rrddir="$work/rrd" --no-cache 2>/dev/null
	ls "$work/rrd/testhost" 2>/dev/null || true
}

# EXSTOREPATTERN drops matching instances at the writer; STOREPATTERN
# keeps only matching ones.
cat >"$work/body-storefilters" <<'BODY'
<!--XYMON METRICS: filt
DS:v:GAUGE:600:0:U
bad 5
good 6
-->
<!--XYMON METRICS: only
DS:v:GAUGE:600:0:U
keep 7
other 8
-->
<!--XYMON METRICS: flz
DS:v:GAUGE:600:0:U
pinned 100
rest 100
-->
status text
BODY
out=$(feed_status diskio "$work/body-storefilters")
assert_not_contains "filt.bad.rrd" "$out" "EXSTOREPATTERN drops the matching instance"
assert_contains "filt.good.rrd" "$out" "EXSTOREPATTERN leaves the others"
assert_contains "only.keep.rrd" "$out" "STOREPATTERN keeps the matching instance"
assert_not_contains "only.other.rrd" "$out" "STOREPATTERN drops non-matching instances"
assert_contains "flz.pinned.rrd" "$out" "STOREPATTERN keeps the matching instance across blocks"
assert_not_contains "flz.rest.rrd" "$out" "STOREPATTERN drops the non-matching instance"

# Creation is eager for every instance, steady or changing.
out=$(feed2 plaingdef steady 4 4 changing 4 9)
assert_contains "plaingdef.steady.rrd" "$out" "the steady instance gets its file"
assert_contains "plaingdef.changing.rrd" "$out" "the changing instance gets its file"

# Markers are line-anchored: a banner quoted mid-line must not trigger the
# writer, and a plain status creates nothing.
cat >"$work/body-midline" <<'EOF'
the docs mention <!--XYMON METRICS: quoted
and that is all
EOF
out=$(feed_status diskio "$work/body-midline")
assert_not_contains ".rrd" "$out" "mid-line banner text does not trigger the writer"

printf 'all green\n' >"$work/body-plain"
out=$(feed_status diskio "$work/body-plain")
assert_not_contains ".rrd" "$out" "plain status without markers creates nothing"

# Dialect extensibility: a DS spec may declare a unit as an optional 7th
# colon field - the writer must strip it before rrdtool sees the spec, or
# file creation fails. A declaration line the writer does not know (an
# ALL-CAPS keyword ending in ':', here a future THRESHOLD:) is ignored:
# no file for it, and the instances around it are written normally.
cat >"$work/body-dialect" <<'EOF'
<!--XYMON METRICS: temperature
DS:temp:GAUGE:1200:-30:50:degC DS:hi:GAUGE:600:-30:50
THRESHOLD:temp:>hi:warn
cpu 47:70
ambient 22:35
-->
temperatures OK
EOF
out=$(feed_status diskio "$work/body-dialect")
assert_contains "temperature.cpu.rrd" "$out" "unit-suffixed DS spec still creates the file"
assert_contains "temperature.ambient.rrd" "$out" "instance after a declaration line written normally"
assert_not_contains "THRESHOLD" "$out" "unknown declaration line creates no file"
# The declared unit, heartbeats AND the THRESHOLD relation land in the
# fileset index (units only for the DS that has one; heartbeats for every
# declared DS; the relation validated against the block)
grep -q 'temperature\.cpu\.rrd [0-9]* u=temp:degC h=temp:1200,hi:600 d=temp,hi t=temp:>hi:warn g=[0-9]*$' "$work/rrd/testhost/.fileset-index" \
	|| fail "declared unit/heartbeat/threshold not recorded in the fileset index: $(cat "$work/rrd/testhost/.fileset-index")"

# A redeclared heartbeat replaces the record outright (strong, complete
# spec) - the schema-reconcile tool trusts h= as the CURRENT declaration.
sed 's/DS:temp:GAUGE:1200/DS:temp:GAUGE:900/' "$work/body-dialect" >"$work/body-dialect2"
feed_status diskio "$work/body-dialect2" >/dev/null
grep -q 'temperature\.cpu\.rrd [0-9]* u=temp:degC h=temp:900,hi:600 ' "$work/rrd/testhost/.fileset-index" \
	|| fail "redeclared heartbeat did not replace h=: $(grep temperature.cpu "$work/rrd/testhost/.fileset-index")"

# ... and a block that STOPS declaring a field retracts it: the next
# sample's bundle is the whole truth, so a dropped THRESHOLD line must
# leave the record instead of riding every fresh generation forever.
sed '/^THRESHOLD:/d' "$work/body-dialect" >"$work/body-dialect3"
ts=$(date +%s)
rm -rf "$work/rrd"; mkdir -p "$work/rrd" "$work/tmp"
{
	printf '@@status|%s|127.0.0.1|origin|testhost|diskio|%s|green||green|%s|0||0||%s|0|linux|/\n' \
		"$ts" $((ts+1800)) "$ts" "$ts"
	cat "$work/body-dialect"
	printf '@@\n'
	printf '@@status|%s|127.0.0.1|origin|testhost|diskio|%s|green||green|%s|0||0||%s|0|linux|/\n' \
		$((ts+300)) $((ts+2100)) "$ts" "$ts"
	cat "$work/body-dialect3"
	printf '@@\n'
} | env XYMONHOME="$work" XYMONTMP="$work/tmp" \
	"$XYMOND_RRD" --rrddir="$work/rrd" --no-cache 2>/dev/null
grep -q 'temperature\.cpu\.rrd [0-9]* u=temp:degC h=temp:1200,hi:600 d=temp,hi g=[0-9]*$' "$work/rrd/testhost/.fileset-index" \
	|| fail "dropped THRESHOLD not retracted from the index: $(grep temperature.cpu "$work/rrd/testhost/.fileset-index")"

# The writer reads at most MAXCOLS (20) columns per line: a block declaring
# 21 DS specs still creates files for instance lines carrying 20 values
# (the marker parser caps its DS count at the same 20 for paging parity).
{
	printf '<!--XYMON METRICS: wide\n'
	printf 'DS:d%d:GAUGE:600:0:U ' $(seq 0 19)
	printf 'DS:d20:GAUGE:600:0:U\n'
	printf 'w0 1:1:1:1:1:1:1:1:1:1:1:1:1:1:1:1:1:1:1:1\n'
	printf -- '-->\n'
	printf 'status text\n'
} >"$work/body-wide"
out=$(feed_status diskio "$work/body-wide")
assert_contains "wide.w0.rrd" "$out" "21st DS spec is ignored (MAXCOLS): a 20-value line still writes"

# Freshness follows COMMIT, not receipt - and for a real file it lives
# in the file's own mtime, not the index: rrdtool only touches the file
# when it accepts the update, so a rejected one (a timestamp behind the
# file's last update, a garbage value) leaves the mtime alone and a
# chronically broken producer goes stale on schedule. The index's
# persisted ts is deliberately NOT rewritten by plain commits (write
# economics): it stays at its creation-flush value.
ts=$(date +%s)
rm -rf "$work/rrd"; mkdir -p "$work/rrd" "$work/tmp"
feed_at() {  # feed_at <statusts> <value> -- one committed-or-rejected sample
	{
		printf '@@status|%s|127.0.0.1|origin|testhost|diskio|%s|green||green|%s|0||0||%s|0|linux|/\n' \
			"$1" $(($1+1800)) "$ts" "$ts"
		printf '<!--XYMON METRICS: frsh\nDS:v:GAUGE:600:0:U\nx %s\n-->\ns\n@@\n' "$2"
	} | env XYMONHOME="$work" XYMONTMP="$work/tmp" \
		"$XYMOND_RRD" --rrddir="$work/rrd" --no-cache 2>/dev/null
}
feed_at "$ts" 5
ts1=$(awk '/^frsh\.x\.rrd /{print $2}' "$work/rrd/testhost/.fileset-index")
[ -n "$ts1" ] || fail "committed update did not stamp the index"
touch -r "$work/rrd/testhost/frsh.x.rrd" "$work/tmp/frsh-ref"
sleep 1	# mtime comparisons below discriminate at second granularity
feed_at $((ts-600)) 6	# behind the file's last update: rrdtool rejects it
[ "$work/rrd/testhost/frsh.x.rrd" -nt "$work/tmp/frsh-ref" ] \
	&& fail "rejected update advanced the file's freshness (mtime)"
# The discriminating case: a NEWER timestamp whose value rrdtool rejects.
feed_at $((ts+150)) not-a-number
[ "$work/rrd/testhost/frsh.x.rrd" -nt "$work/tmp/frsh-ref" ] \
	&& fail "rejected garbage value advanced the file's freshness (mtime)"
feed_at $((ts+300)) 7
[ "$work/rrd/testhost/frsh.x.rrd" -nt "$work/tmp/frsh-ref" ] \
	|| fail "accepted update did not advance the file's freshness (mtime)"
ts2=$(awk '/^frsh\.x\.rrd /{print $2}' "$work/rrd/testhost/.fileset-index")
[ "$ts2" = "$ts1" ] || fail "a plain commit rewrote the index ($ts1 -> $ts2): write economics regressed"

# Drop barrier: a straggler message already queued behind @@drophost must
# not recreate files - or the fileset index - inside the deleted host
# directory (the deletion itself is forked, so a recreated file also
# races it). The barrier discards messages for a recently dropped host.
ts=$(date +%s)
rm -rf "$work/rrd"; mkdir -p "$work/rrd" "$work/tmp"
{
	printf '@@status|%s|127.0.0.1|origin|testhost|diskio|%s|green||green|%s|0||0||%s|0|linux|/\n' \
		"$ts" $((ts+1800)) "$ts" "$ts"
	printf '<!--XYMON METRICS: dropme\nDS:v:GAUGE:600:0:U\nx 5\n-->\ns\n@@\n'
	printf '@@drophost|%s|127.0.0.1|testhost\n@@\n' "$ts"
	# Wait for the forked deletion to FINISH before the straggler
	# arrives - the losing interleaving, where a recreated file has
	# nothing left to clean it up. (A fixed sleep would let the child's
	# rm run last on a loaded box and hide the recreation by timing
	# luck.) Bounded: ~10s, then the straggler goes in regardless.
	i=0
	while [ -e "$work/rrd/testhost" ] && [ $i -lt 100 ]; do sleep 0.1; i=$((i+1)); done
	printf '@@status|%s|127.0.0.1|origin|testhost|diskio|%s|green||green|%s|0||0||%s|0|linux|/\n' \
		$((ts+1)) $((ts+1801)) "$ts" "$ts"
	printf '<!--XYMON METRICS: dropme\nDS:v:GAUGE:600:0:U\nx 6\n-->\ns\n@@\n'
} | env XYMONHOME="$work" XYMONTMP="$work/tmp" \
	"$XYMOND_RRD" --rrddir="$work/rrd" --no-cache 2>/dev/null
# A deletion forked for a recreated directory may still be running when
# xymond_rrd exits - wait for the condition, not a wall-clock guess. A
# directory recreated AFTER the deletion finished (the barrier bug this
# guards) has nothing left to remove it, so it persists past the timeout
# and fails below.
i=0
while [ -e "$work/rrd/testhost" ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i+1)); done
[ -e "$work/rrd/testhost" ] \
	&& fail "straggler recreated the dropped host directory: $(ls "$work/rrd/testhost")"

# renamehost: pending CACHED updates must flush into the old-named files
# before the rename moves them (rrdcacheflushhost cannot do this: it
# expects "/host"-shaped keys and rate-limits; a call with a bare
# hostname is a silent no-op).
rm -rf "$work/rrd"; mkdir -p "$work/rrd" "$work/tmp"
{
	printf '@@status|%s|127.0.0.1|origin|testhost|diskio|%s|green||green|%s|0||0||%s|0|linux|/\n' \
		"$ts" $((ts+1800)) "$ts" "$ts"
	printf '<!--XYMON METRICS: renm\nDS:v:GAUGE:600:0:U\nx 5\n-->\ns\n@@\n'
	printf '@@renamehost|%s|127.0.0.1|testhost|newhost\n@@\n' "$ts"
} | env XYMONHOME="$work" XYMONTMP="$work/tmp" \
	"$XYMOND_RRD" --rrddir="$work/rrd" --debug >"$work/dbg.log" 2>&1
[ -d "$work/rrd/newhost" ] || fail "rename did not move the host directory"
grep -q "flushed and dropped 1 entries for host testhost" "$work/dbg.log" \
	|| fail "pending update not flushed before the rename: $(grep -i updcache "$work/dbg.log")"

# Every block form creates eagerly: plain METRICS, one carrying an
# unknown attribute (ignored), and the legacy DEVMON banner alike.
ts=$(date +%s)
rm -rf "$work/rrd"; mkdir -p "$work/rrd" "$work/tmp"
{
	printf '@@status|%s|127.0.0.1|origin|testhost|diskio|%s|green||green|%s|0||0||%s|0|linux|/\n' \
		"$ts" $((ts+1800)) "$ts" "$ts"
	printf '<!--XYMON METRICS: ld\nDS:v:GAUGE:600:0:U\nx 5\n-->\n'
	printf '<!--XYMON METRICS: ldno opt=later\nDS:v:GAUGE:600:0:U\ny 6\n-->\ns\n@@\n'
	printf '@@status|%s|127.0.0.1|origin|testhost|diskio|%s|green||green|%s|0||0||%s|0|linux|/\n' \
		$((ts+300)) $((ts+2100)) "$ts" "$ts"
	printf '<!--DEVMON RRD: lddev\nDS:v:GAUGE:600:0:U\nz 7\n-->\ns\n@@\n'
} | env XYMONHOME="$work" XYMONTMP="$work/tmp" \
	"$XYMOND_RRD" --rrddir="$work/rrd" --no-cache 2>/dev/null
[ -f "$work/rrd/testhost/ld.x.rrd" ] \
	|| fail "a plain METRICS block must create its file on the first sample"
[ -f "$work/rrd/testhost/ldno.y.rrd" ] \
	|| fail "an unknown banner attribute must be ignored, not break the block"
[ -f "$work/rrd/testhost/lddev.z.rrd" ] \
	|| fail "legacy DEVMON banner must create eagerly"

# Deep-review regressions: (1) a legacy DEVMON block may carry instances
# named like a declaration keyword - the METRICS-only contract must not
# drop them; (2) a METRICS block without a DS line writes nothing and
# must not inherit the previous block's DS params; (3) a self-closed
# one-line banner is an empty block - the status text after it must not
# be consumed as instance data.
cat >"$work/body-regress" <<'EOF'
<!--DEVMON RRD: if_load 0 0
DS:ds0:COUNTER:600:0:U DS:ds1:COUNTER:600:0:U
CPU:1 47:70
-->
<!--XYMON METRICS: goodblock
DS:v:GAUGE:600:0:U DS:w:GAUGE:600:0:U
full 1:2
short 1
-->
<!--XYMON METRICS: nodsblock
x 5
y 6
-->
<!--XYMON METRICS: selfclosed -->
oops 7
status text
EOF
out=$(feed_status devtest "$work/body-regress")
# Lazy gate vs unknown values: "U" and "0" are DIFFERENT samples - an
# instance whose probe failed (U baseline) and then reports 0 has
# changed and must get its file (numeric-only comparison equated them).
ts=$(date +%s)
rm -rf "$work/rrd"; mkdir -p "$work/rrd" "$work/tmp"
{
	printf '@@status|%s|127.0.0.1|origin|testhost|diskio|%s|green||green|%s|0||0||%s|0|linux|/\n' \
		"$ts" $((ts+1800)) "$ts" "$ts"
	printf '<!--XYMON METRICS: lzu lazy\nDS:v:GAUGE:600:0:U\nx U\n-->\ns\n@@\n'
	printf '@@status|%s|127.0.0.1|origin|testhost|diskio|%s|green||green|%s|0||0||%s|0|linux|/\n' \
		$((ts+300)) $((ts+2100)) "$ts" "$ts"
	printf '<!--XYMON METRICS: lzu lazy\nDS:v:GAUGE:600:0:U\nx 0\n-->\ns\n@@\n'
} | env XYMONHOME="$work" XYMONTMP="$work/tmp" \
	"$XYMOND_RRD" --rrddir="$work/rrd" --no-cache 2>/dev/null
[ -f "$work/rrd/testhost/lzu.x.rrd" ] \
	|| fail "lazy: a U -> 0 transition is a change and must create the file"

assert_contains "if_load.CPU:1.rrd" "$out" "legacy devmon keyword-named instance still written"
assert_contains "goodblock.full.rrd" "$out" "normal instance in a 2-DS block written"
assert_not_contains "goodblock.short" "$out" "instance with too few values skipped"
assert_not_contains "nodsblock" "$out" "block without a DS line writes nothing"
assert_not_contains "selfclosed" "$out" "self-closed banner opens no block"
assert_not_contains ".oops." "$out" "status text after a self-closed banner is not instance data"

# The writer-kept fileset index: every RRD write is bookkept into
# <host>/.fileset-index (a durable home for display counts and, later,
# units/thresholds/lazy baselines). A deleted index is reseeded from a
# one-off directory scan, so pre-existing files reappear in it.
ts=$(date +%s)
rm -rf "$work/rrd"; mkdir -p "$work/rrd" "$work/tmp"
{
	printf '@@status|%s|127.0.0.1|origin|testhost|diskio|%s|green||green|%s|0||0||%s|0|linux|/\n' \
		"$ts" $((ts+1800)) "$ts" "$ts"
	printf '<!--XYMON METRICS: fsx\nDS:v:GAUGE:600:0:U\na 1\nb 2\n-->\ns\n@@\n'
} | env XYMONHOME="$work" XYMONTMP="$work/tmp" \
	"$XYMOND_RRD" --rrddir="$work/rrd" --no-cache 2>/dev/null
idx="$work/rrd/testhost/.fileset-index"
[ -f "$idx" ] || fail "fileset index not written"
grep -q '^fsx\.a\.rrd [0-9]' "$idx" || fail "index misses a written file: $(cat "$idx")"
grep -q '^fsx\.b\.rrd [0-9]' "$idx" || fail "index misses a written file"

rm -f "$idx"
{
	printf '@@status|%s|127.0.0.1|origin|testhost|diskio|%s|green||green|%s|0||0||%s|0|linux|/\n' \
		$((ts+300)) $((ts+2100)) "$ts" "$ts"
	printf '<!--XYMON METRICS: fsy\nDS:v:GAUGE:600:0:U\nc 3\n-->\ns\n@@\n'
} | env XYMONHOME="$work" XYMONTMP="$work/tmp" \
	"$XYMOND_RRD" --rrddir="$work/rrd" --no-cache 2>/dev/null
grep -q '^fsy\.c\.rrd [0-9]' "$idx" || fail "rebuilt index misses the new write"
grep -q '^fsx\.a\.rrd [0-9]' "$idx" || fail "rebuilt index misses pre-existing files (scan seed): $(cat "$idx")"

# Derived consolidations: a gdef whose FNPATTERN matches the file and
# whose DEFs read :MAX makes the writer clone the AVERAGE archives as
# MAX at creation; a file no gdef reads beyond AVERAGE gets exactly the
# stock set. (Requires the rrdtool CLI to inspect the created file.)
if command -v rrdtool >/dev/null 2>&1; then
	ts=$(date +%s)
	rm -rf "$work/rrd"; mkdir -p "$work/rrd" "$work/tmp"
	{
		printf '@@status|%s|127.0.0.1|origin|testhost|diskio|%s|green||green|%s|0||0||%s|0|linux|/\n' \
			"$ts" $((ts+1800)) "$ts" "$ts"
		printf '<!--XYMON METRICS: cfx\nDS:v:GAUGE:600:0:U\na 1\n-->\ns\n@@\n'
		printf '@@status|%s|127.0.0.1|origin|testhost|diskio|%s|green||green|%s|0||0||%s|0|linux|/\n' \
			$((ts+1)) $((ts+1801)) "$ts" "$ts"
		printf '<!--XYMON METRICS: cfplain\nDS:v:GAUGE:600:0:U\nx 1\n-->\ns\n@@\n'
	} | env XYMONHOME="$work" XYMONTMP="$work/tmp" \
		"$XYMOND_RRD" --rrddir="$work/rrd" --no-cache 2>/dev/null
	rrdtool info "$work/rrd/testhost/cfx.a.rrd" | grep -q 'cf = "MAX"' \
		|| fail "gdef-declared MAX archives not derived at creation"
	rrdtool info "$work/rrd/testhost/cfx.a.rrd" | grep -q 'cf = "AVERAGE"' \
		|| fail "AVERAGE archives must remain alongside derived ones"
	rrdtool info "$work/rrd/testhost/cfplain.x.rrd" | grep -q 'cf = "MAX"' \
		&& fail "a file no gdef reads beyond AVERAGE must keep the stock archive set"
fi

# Crash-leftover rebuild: a zero-length index (interrupted flush) must
# reseed from the directory scan, exactly like a missing one. Pin a file
# that is actually on disk - which set that is depends on whether the
# (rrdtool-gated) CF section above ran and reset the directory.
preexisting=$(basename "$(ls "$work/rrd/testhost/"*.rrd | head -1)")
: >"$idx"
{
	printf '@@status|%s|127.0.0.1|origin|testhost|diskio|%s|green||green|%s|0||0||%s|0|linux|/\n' \
		$((ts+600)) $((ts+2400)) "$ts" "$ts"
	printf '<!--XYMON METRICS: fsz\nDS:v:GAUGE:600:0:U\nd 4\n-->\ns\n@@\n'
} | env XYMONHOME="$work" XYMONTMP="$work/tmp" \
	"$XYMOND_RRD" --rrddir="$work/rrd" --no-cache 2>/dev/null
grep -q "^${preexisting//./\\.} [0-9]" "$idx" \
	|| fail "empty index file not rebuilt by the scan ($preexisting missing): $(cat "$idx")"
grep -q '^fsz\.d\.rrd [0-9]' "$idx" || fail "rebuilt index misses the triggering write"

# The block writer carries a pre-cutover legacy file across (do_disk's
# one-time migration, ported): after the rename, only the encoded file
# remains - no frozen legacy curve graphing next to a restarted one.
rm -rf "$work/rrd"; mkdir -p "$work/rrd" "$work/tmp"
{
	printf '@@status|%s|127.0.0.1|origin|testhost|diskio|%s|green||green|%s|0||0||%s|0|linux|/\n' \
		"$ts" $((ts+1800)) "$ts" "$ts"
	printf '<!--XYMON METRICS: mig\nDS:v:GAUGE:600:0:U\n/var 1\n-->\ns\n@@\n'
} | env XYMONHOME="$work" XYMONTMP="$work/tmp" \
	"$XYMOND_RRD" --rrddir="$work/rrd" --no-cache 2>/dev/null
[ -f "$work/rrd/testhost/mig.%2Fvar.rrd" ] || fail "migration setup: encoded file not created"
mv "$work/rrd/testhost/mig.%2Fvar.rrd" "$work/rrd/testhost/mig,var.rrd"
rm -f "$work/rrd/testhost/.fileset-index"
{
	printf '@@status|%s|127.0.0.1|origin|testhost|diskio|%s|green||green|%s|0||0||%s|0|linux|/\n' \
		$((ts+300)) $((ts+2100)) "$ts" "$ts"
	printf '<!--XYMON METRICS: mig\nDS:v:GAUGE:600:0:U\n/var 2\n-->\ns\n@@\n'
} | env XYMONHOME="$work" XYMONTMP="$work/tmp" \
	"$XYMOND_RRD" --rrddir="$work/rrd" --no-cache 2>/dev/null
[ -f "$work/rrd/testhost/mig.%2Fvar.rrd" ] || fail "legacy block file not migrated to the encoded name"
[ -e "$work/rrd/testhost/mig,var.rrd" ] && fail "legacy file left behind - every mount would graph twice"

# Dispatch precedence (self-describing beats built-in): a status whose
# column has a built-in handler but which carries a store block is
# written by the block writer ONLY - the built-in must not double-write.
# A block-less status on the same column hits the built-in unchanged.
cat >"$work/body-diskblock" <<'EOF'
Filesystem 1024-blocks Used Available Capacity Mounted on
/dev/sda1 100 50 50 50% /var
<!--XYMON METRICS: diskpct
DS:pct:GAUGE:600:0:100
/var 50
-->
EOF
out=$(feed_status disk "$work/body-diskblock")
assert_contains "diskpct.%2Fvar.rrd" "$out" "block on a built-in column is written by the block writer"
assert_not_contains "disk.%2Fvar.rrd" "$out" "built-in disk handler must not double-write a block-bearing status"

cat >"$work/body-diskplain" <<'EOF'
Filesystem 1024-blocks Used Available Capacity Mounted on
/dev/sda1 100 50 50 50% /var
EOF
out=$(feed_status disk "$work/body-diskplain")
assert_contains "disk.%2Fvar.rrd" "$out" "block-less disk status hits the built-in handler unchanged"

# A banner the writer would REJECT (invalid name) must not divert routing:
# the built-in handler still runs, instead of storing nothing at all.
cat >"$work/body-badname" <<'EOF'
Filesystem 1024-blocks Used Available Capacity Mounted on
/dev/sda1 100 50 50 50% /var
<!--XYMON METRICS: ../evil
DS:v:GAUGE:600:0:U
x 1
-->
EOF
out=$(feed_status disk "$work/body-badname")
assert_contains "disk.%2Fvar.rrd" "$out" "invalid-name block falls back to the built-in handler"

pass "XYMON METRICS blocks and legacy DEVMON banners are written by content routing"
