#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/rrd/disk-typecolumn-layout.sh
#
# do_disk_rrd() picks its column layout by sniffing the message text, not by
# the host's declared OS. One of those layouts is a df listing that carries a
# filesystem-type column, which shifts every field one to the right:
#
#     Filesystem Type 1024-blocks Used Available Capacity Mounted on
#     /dev/sda1  ext4 10240       512  9728      5%       /
#
# ext4 deliberately: the branch used to be selected from a list of type names
# (xfs, efs, cxfs - the IRIX df it was written for), so a Linux "df -PT" fell
# through to the plain layout and produced exactly the defect below. It is
# selected from the header now.
#
# It was called DT_IRIX because IRIX's df produced it, and the platform
# cleanup for issue #85 read the name as a platform and removed it. It is not:
# "df -PT" produces the same shape on Linux today, and any reporter that shells
# out to it lands here regardless of its OS.
#
# Routed to DT_UNIX instead, the capacity column is read as the mount point:
# the RRD is created as "disk5%.rrd" -- a new file per distinct percentage --
# and the parsed pct of 9728 falls outside DS:pct:GAUGE:600:0:100 and stores
# NaN. Both the file name and the stored value are asserted, because either
# alone would pass on a build that got the other half right.
#
# No shipped client emits a type column (linux uses df -P -l -i -x, the BSDs
# -t no<csv>, aix -Ik, hp-ux -Pk, sunos -F -k), so nothing in CI exercises this
# unless a test does.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin XYMOND_RRD "xymond/xymond_rrd"
require_cc
require_gnu_make
ROOT=$(find_root)

work=$(mktempdir)
ts=$(date +%s)
mkdir -p "$work/tmp" "$work/rrd" "$work/home/etc"
: > "$work/home/etc/analysis.cfg"
: > "$work/hosts.cfg"

{
	printf '@@status|%s|127.0.0.1|origin|testhost|disk|%s|green||green|%s|0||0||%s|0|linux|/\n' \
		"$ts" "$((ts+1800))" "$ts" "$ts"
	printf 'disk report\n'
	printf 'Filesystem Type 1024-blocks Used Available Capacity Mounted on\n'
	printf '/dev/sda1 ext4 10240 512 9728 5%% /\n'
	printf '@@\n'
} | env XYMONHOME="$work/home" XYMONTMP="$work/tmp" XYMONRUNDIR="$work/tmp" \
	HOSTSCFG="$work/hosts.cfg" \
	"$XYMOND_RRD" --rrddir="$work/rrd" >"$work/worker.log" 2>&1 \
	|| { cat "$work/worker.log" >&2; fail "the worker exited non-zero"; }

[ -d "$work/rrd/testhost" ] || { cat "$work/worker.log" >&2; fail "no RRD directory was created"; }

got=$(ls "$work/rrd/testhost")
assert_equal "disk,root.rrd" "$got" \
	"the mount point was not read from the right column -- a type-column df was routed to the plain layout"

# The name being right is not enough: the percentage must have come from the
# capacity column too, not from the block counts beside it.
# Read through librrd, not the rrdtool CLI: the command-line tool is a separate
# package and is not installed everywhere the suite runs, and gating on it
# means the half that matters here -- the percentage, read from the capacity
# column -- silently goes unchecked. tests/rrd/inode-unix-columns-harness.c
# reads a last update the same way, and this uses the same harness.
buildflags_output=$("$XYMON_MAKE" -s -C "$ROOT" -f Makefile -f - disk-test-flags <<'EOF'
.PHONY: disk-test-flags
disk-test-flags:
	@printf '%s\n' 'ldflags=$(LDFLAGS)' 'rpathopt=$(RPATHOPT)' \
		'rrddef=$(RRDDEF)' 'rrdincdir=$(RRDINCDIR)' 'rrdlibs=$(RRDLIBS)'
EOF
) || fail "cannot read configured RRD build flags"
buildflags=()
while IFS= read -r line; do buildflags+=("$line"); done <<< "$buildflags_output"
[ "${#buildflags[@]}" -eq 5 ] || fail "configured RRD build flags are incomplete"
ldflags=${buildflags[0]#ldflags=}
rpathopt=${buildflags[1]#rpathopt=}
rrddef=${buildflags[2]#rrddef=}
rrdincdir=${buildflags[3]#rrdincdir=}
rrdlibs=${buildflags[4]#rrdlibs=}
[ -n "$rrdlibs" ] || rrdlibs="-lrrd"

# shellcheck disable=SC2086  # configured flags are deliberate word-split lists
"$CC" $ldflags -iquote "$ROOT/include" $rrddef $rrdincdir \
	-o "$work/rrd-lastupdate" $rpathopt \
	"$ROOT/tests/rrd/inode-unix-columns-harness.c" $rrdlibs \
	2>"$work/cc.log" || { cat "$work/cc.log" >&2; fail "librrd last-update harness does not compile"; }

# lastupdate, not fetch: a single update does not fill a consolidation
# interval, so fetch reports NaN even when the value landed.
pct=$("$work/rrd-lastupdate" "$work/rrd/testhost/disk,root.rrd" 2>/dev/null \
	| awk '/^[0-9]+:/ { print $2; exit }')
[ -n "$pct" ] || fail "the RRD holds no reading at all"
assert_equal "5" "$pct" \
	"the stored percentage did not come from the capacity column"

pass "a df listing with a filesystem-type column keeps its own column layout"
