#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/rrd/rrddef-step-argc.sh
#
# create_and_update_rrd() sizes its argv as "rrdcreate <file> -s <step>" plus
# the parameters and RRA definitions, but when rrddefinitions.cfg carries its
# own step setting the "-s <step>" pair is left out and fixcount stays 2 --
# while the call still passed 4+pcount as argc. librrd then reads the two
# calloc'd NULL slots after the real arguments as argv entries and crashes
# (segfault measured on RRDtool 1.7.2), so one configured step setting kills
# xymond_rrd on the first RRD it creates.
#
# The case: a [disk] definition whose first lines are the step setting, fed a
# production disk status message. The RRD must exist afterwards; on the
# over-counted argc the worker dies before creating it.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)

require_bin XYMOND_RRD xymond/xymond_rrd

WORK=$(mktempdir)
mkdir -p "$WORK/etc" "$WORK/tmp" "$WORK/rrd"
now=$(date +%s)

# Each line is one argv entry for rrd_create, so the step option and its value
# are separate lines -- the form do_rrd.c's havestepsetting check looks for.
cat > "$WORK/etc/rrddefinitions.cfg" <<'EOF'
[disk]
	-s
	60
	RRA:AVERAGE:0.5:1:576
EOF

{
	printf '@@status|%s|127.0.0.1|origin|testhost|disk|%s|green||green|%s|0||0||%s|0|linux|/\n' \
		"$now" "$((now+1800))" "$now" "$now"
	printf 'disk report\n'
	printf '/dev/sda1 1000000 400000 600000 40%% /data\n'
	printf '@@\n'
} | env XYMONHOME="$WORK" XYMONTMP="$WORK/tmp" \
	"$XYMOND_RRD" --rrddir="$WORK/rrd" --no-cache 2>"$WORK/worker.log" \
	|| { sed -n '1,10p' "$WORK/worker.log" >&2
	     fail "xymond_rrd died on a disk status while rrddefinitions.cfg carries its own step setting"; }

[ -f "$WORK/rrd/testhost/disk,data.rrd" ] \
	|| { ls -l "$WORK/rrd/testhost" >&2 || true
	     fail "no RRD created: the create argv was over-counted when the step setting comes from rrddefinitions.cfg"; }

pass "a rrddefinitions.cfg step setting no longer over-counts the rrd_create argv"
