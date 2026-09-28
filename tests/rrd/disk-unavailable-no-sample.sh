#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/rrd/disk-unavailable-no-sample.sh
#
# When a remote mount is unreachable the client reports it as a df-shaped row,
# "srv:/exp - - - 100% /mnt": it names the device, so the operator can see which
# server went away, and reports 100% so the disk column goes red instead of the
# whole host going purple. The sizes are "-" because nothing measured them.
#
# Trending it therefore writes 100% used and 1 KB for as long as the server is
# unreachable -- a full filesystem that was never measured, with the pre-outage
# values buried behind it. The RRD must keep its last real sample instead (#344).
#
# Drives the real xymond_rrd, as tests/rrd/inode-unix-columns.sh does, over two
# messages: one with a genuine reading, then one where the same mount has gone
# unavailable. The second must not move the RRD.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin XYMOND_RRD xymond/xymond_rrd

ROOT=$(find_root)
require_c_buildenv "$ROOT"
[ -f "$ROOT/Makefile" ] || skip "tree not configured (no Makefile)"

work=$(mktempdir)
mkdir -p "$work/home/etc" "$work/tmp" "$work/rrd"
: > "$work/home/etc/analysis.cfg"
: > "$work/hosts.cfg"

buildflags_output=$("$XYMON_MAKE" -s -C "$ROOT" -f Makefile -f - disk-test-flags <<'EOF'
.PHONY: disk-test-flags
disk-test-flags:
	@printf '%s\n' 'ldflags=$(LDFLAGS)' 'rpathopt=$(RPATHOPT)' \
		'rrddef=$(RRDDEF)' 'rrdincdir=$(RRDINCDIR)' 'rrdlibs=$(RRDLIBS)'
EOF
) || fail "cannot read configured RRD build flags"
# Read the lines into an array without mapfile: that is a bash 4 builtin
# and macOS ships bash 3.2, where the suite runs under /usr/bin/env bash.
buildflags=()
while IFS= read -r line; do buildflags+=("$line"); done <<< "$buildflags_output"
[ "${#buildflags[@]}" -eq 5 ] || fail "configured RRD build flags are incomplete"
ldflags=${buildflags[0]#ldflags=}
rpathopt=${buildflags[1]#rpathopt=}
rrddef=${buildflags[2]#rrddef=}
rrdincdir=${buildflags[3]#rrdincdir=}
rrdlibs=${buildflags[4]#rrdlibs=}
[ -n "$rrdlibs" ] || rrdlibs="-lrrd"

# The last-update reader is the one tests/rrd/inode-unix-columns.sh builds; it
# takes any RRD, so there is no second harness to keep in step.
# Configured flags are deliberate word-split lists.
# shellcheck disable=SC2086
"$CC" $ldflags -iquote "$ROOT/include" $rrddef $rrdincdir \
	-o "$work/rrd-lastupdate" $rpathopt \
	"$ROOT/tests/rrd/inode-unix-columns-harness.c" $rrdlibs \
	2>"$work/cc.log" || {
	cat "$work/cc.log" >&2
	fail "librrd last-update harness does not compile"
}

# send_disk TIMESTAMP REMOTE-ROW -- one disk status message through xymond_rrd.
# The local filesystem is identical in both, so anything that moves belongs to
# the remote one.
send_disk() {
	printf '%s' "@@status|$1|127.0.0.1||unix.test|disk|$(($1 + 1800))|green||||||||||unix|
status unix.test.disk green $1 - Filesystems ok
Filesystem 1024-blocks Used Available Capacity Mounted on
/dev/sda1 1000000 400000 600000 40% /
$2
@@
" | env \
		XYMONHOME="$work/home" \
		XYMONVAR="$work" \
		XYMONTMP="$work/tmp" \
		XYMONRUNDIR="$work/tmp" \
		XYMONRRDS="$work/rrd" \
		HOSTSCFG="$work/hosts.cfg" \
		TEST2RRD="disk" \
		GRAPHS="disk" \
		"$XYMOND_RRD" --no-cache --rrddir="$work/rrd" \
		>>"$work/xymond_rrd.log" 2>&1
}

# The mount is measured first, so the RRD holds a real sample to protect. A
# reading well away from the sentinel's 100%/1 makes a fabricated sample
# unmistakable.
first=$(date +%s)
send_disk "$first" "srv:/exp 200000 50000 150000 25% /remote/nfs"

rrd="$work/rrd/unix.test/disk,remote,nfs.rrd"
assert_file_exists "$rrd" "the measured remote mount is trended"
before=$("$work/rrd-lastupdate" "$rrd")
assert_match "${first}:[[:space:]]+25([.]0+)?[[:space:]]+50000([.]0+)?" "$before" \
	"the measured reading reaches the RRD"

# The server has gone away: same mount, sentinel row. Five minutes on, so a
# sample taken here would land in its own step rather than being folded into
# the one above.
second=$((first + 300))
send_disk "$second" "srv:/exp - - - 100% /remote/nfs"

after=$("$work/rrd-lastupdate" "$rrd")
assert_equal "$before" "$after" \
	"an unmeasured mount was trended: the RRD moved to the sentinel's placeholder 100%/1 instead of keeping the last reading it actually had"

# The local filesystem must be unaffected -- the skip is for the sentinel row,
# not for the message that carries it.
localrrd="$work/rrd/unix.test/disk,root.rrd"
assert_file_exists "$localrrd" "the local filesystem is still trended"
assert_match "${second}:" "$("$work/rrd-lastupdate" "$localrrd")" \
	"the local filesystem took the second cycle's sample"

# The inode column is trended by the same do_disk_rrd() (xymond/do_rrd.c), and
# the BSD and darwin clients carry the inode fields in the sentinel row too, so
# the same row has to be skipped on that path as well.
send_inode() {
	printf '%s' "@@status|$1|127.0.0.1||unix.test|inode|$(($1 + 1800))|green||||||||||unix|
status unix.test.inode green $1 - Filesystems ok
Filesystem itotal iused ifree %iused Mounted on
$2
@@
" | env \
		XYMONHOME="$work/home" \
		XYMONVAR="$work" \
		XYMONTMP="$work/tmp" \
		XYMONRUNDIR="$work/tmp" \
		XYMONRRDS="$work/rrd" \
		HOSTSCFG="$work/hosts.cfg" \
		TEST2RRD="inode" \
		GRAPHS="inode" \
		"$XYMOND_RRD" --no-cache --rrddir="$work/rrd" \
		>>"$work/xymond_rrd.log" 2>&1
}

send_inode "$first" "srv:/exp 259070 41020 218050 15% /remote/nfs"
irrd="$work/rrd/unix.test/inode,remote,nfs.rrd"
assert_file_exists "$irrd" "the measured remote mount is trended for inodes"
ibefore=$("$work/rrd-lastupdate" "$irrd")
assert_match "${first}:[[:space:]]+15([.]0+)?[[:space:]]+41020([.]0+)?" "$ibefore" \
	"the measured inode reading reaches the RRD"

# The BSD and darwin sentinels emit the marker with the inode columns too,
# but every client's [inode] pipeline collapses it before it is sent: what
# reaches the server is the same six-field shape as the disk report.
send_inode "$second" "srv:/exp - - - 100% /remote/nfs"
assert_equal "$ibefore" "$("$work/rrd-lastupdate" "$irrd")" \
	"an unmeasured mount was trended for inodes: the RRD took the sentinel row's placeholder instead of keeping its last reading"

# The skip is on the values, not on the device name, so a server that happens
# to be called "unavailable" is trended like any other: dropping this reading
# would lose the graph for a filesystem that is up and answering.
third=$((second + 300))
send_disk "$third" "unavailable:/export 200000 50000 150000 25% /real/nfs"
realrrd="$work/rrd/unix.test/disk,real,nfs.rrd"
assert_file_exists "$realrrd" \
	"a genuine reading from a server named 'unavailable' was mistaken for an unmeasured row and dropped"
assert_match "${third}:[[:space:]]+25([.]0+)?[[:space:]]+50000([.]0+)?" \
	"$("$work/rrd-lastupdate" "$realrrd")" \
	"the genuine reading must be trended: only the values say a row was not measured"

# A spaced device arrives \040-encoded (the client re-encodes it to keep the
# column count). The marker must be recognised the same: the RRD stays, and
# no filesystem is invented.
fourth=$((third + 300))
rrdcount=$(find "$work/rrd" -name '*.rrd' | wc -l)
send_disk "$fourth" 'srv:/team\040share - - - 100% /remote/nfs'
assert_equal "$before" "$("$work/rrd-lastupdate" "$rrd")" \
	"an unmeasured row naming a spaced device moved the RRD"
assert_equal "$((rrdcount))" "$(($(find "$work/rrd" -name '*.rrd' | wc -l)))" \
	"an unmeasured row naming a spaced device created an RRD of its own"

pass "an unreachable mount reports red without writing a fabricated sample to its disk or inode RRD"
