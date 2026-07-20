#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/xymond-rrd-disk-encoding.sh
#
# do_disk stores each mount point under a reversible, collision-free name
# (rrdinstance_encode: '/'->%2F), so "/a/b" and "/a,b" no longer collapse
# onto one file. Existing legacy "disk,<mount>.rrd" files are
# migrated losslessly by an in-daemon rename on the mount's next update.
# Feed real disk statuses to the built xymond_rrd and assert the filenames.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin XYMOND_RRD "xymond/xymond_rrd"

work=$(mktempdir)

# A UNIX df report: first body line is skipped by do_disk, then one line per
# filesystem. No "Filesystem" header, so the parser stays in DT_UNIX.
feed_disk() {  # feed_disk <bodyfile>
	local ts; ts=$(date +%s)
	rm -rf "$work/rrd"; mkdir -p "$work/rrd" "$work/tmp"
	{
		printf '@@status|%s|127.0.0.1|origin|testhost|disk|%s|green||green|%s|0||0||%s|0|linux|/\n' \
			"$ts" $((ts+1800)) "$ts" "$ts"
		cat "$1"
		printf '@@\n'
	} | env XYMONHOME="$work" XYMONTMP="$work/tmp" \
		"$XYMOND_RRD" --rrddir="$work/rrd" --no-cache 2>/dev/null
	ls "$work/rrd/testhost" 2>/dev/null || true
}

cat >"$work/df" <<'EOF'
disk report (this summary line is skipped)
/dev/sda1 1000000 400000 600000 40% /
/dev/sda2 1000000 400000 600000 40% /var
/dev/sdb1 1000000 400000 600000 40% /var/log
/dev/sdc1 1000000 400000 600000 40% /data,x
EOF
out=$(feed_disk "$work/df")

# Each mount is reversibly encoded; '/' is always %2F.
assert_contains "disk.%2F.rrd"          "$out" "root '/' encodes to %2F"
assert_contains "disk.%2Fvar.rrd"       "$out" "'/var' encodes to %2Fvar"
assert_contains "disk.%2Fvar%2Flog.rrd" "$out" "'/var/log' encodes each slash"
assert_contains "disk.%2Fdata,x.rrd"    "$out" "a literal comma stays a comma - distinct from %2F, so no alias"

# The lossy legacy names must be gone - that shape is what aliased mounts.
assert_not_contains "disk,var.rrd"  "$out" "legacy comma name no longer produced"
assert_not_contains "disk,root.rrd" "$out" "legacy ',root' name no longer produced"

# "/var/log" and a hypothetical "/var,log" must not collide: the encoded forms
# differ (%2Fvar%2Flog vs %2Fvar%2Clog), which was the whole point.
cat >"$work/df2" <<'EOF'
disk report
/dev/sdb1 1000000 400000 600000 40% /var,log
EOF
out=$(feed_disk "$work/df2")
assert_contains "disk.%2Fvar,log.rrd" "$out" "'/var,log' (%2Fvar,log) stays distinct from '/var/log' (%2Fvar%2Flog)"

# Auto-rename migration: a pre-existing legacy file is carried to the encoded
# name on the next update (we hold the live mount point, so it is exact and
# lossless - the RRD history moves with it). feed_disk wipes rrd/, so drive
# xymond_rrd directly here after planting the legacy file.
ts=$(date +%s)
rm -rf "$work/rrd"; mkdir -p "$work/rrd/testhost" "$work/tmp"
printf 'legacy-rrd-history\n' >"$work/rrd/testhost/disk,var.rrd"
{
	printf '@@status|%s|127.0.0.1|origin|testhost|disk|%s|green||green|%s|0||0||%s|0|linux|/\n' \
		"$ts" $((ts+1800)) "$ts" "$ts"
	printf 'disk report\n/dev/sda2 1000000 400000 600000 40%% /var\n'
	printf '@@\n'
} | env XYMONHOME="$work" XYMONTMP="$work/tmp" \
	"$XYMOND_RRD" --rrddir="$work/rrd" --no-cache 2>/dev/null

[ -f "$work/rrd/testhost/disk.%2Fvar.rrd" ] \
	|| fail "auto-rename: legacy disk,var.rrd was not carried to disk.%2Fvar.rrd"
[ -e "$work/rrd/testhost/disk,var.rrd" ] \
	&& fail "auto-rename: legacy disk,var.rrd should be gone after the rename"
grep -q 'legacy-rrd-history' "$work/rrd/testhost/disk.%2Fvar.rrd" \
	|| fail "auto-rename: the original file content (history) was not preserved"

# The legacy writer never shortened filenames - names up to NAME_MAX went
# to disk verbatim. A legacy file in the [NAME_MAX-50, NAME_MAX) range must
# be found under its raw name (the new writer's md5 shortening applies only
# to the rename TARGET), or its history silently restarts.
longtail=$(printf 'v%.0s' $(seq 1 220))
longmount="/$longtail"
legacyfn="disk,$longtail.rrd"	# 229 chars: shorten-threshold (205) < len < NAME_MAX
ts=$(date +%s)
rm -rf "$work/rrd"; mkdir -p "$work/rrd/testhost" "$work/tmp"
printf 'long-legacy-history\n' >"$work/rrd/testhost/$legacyfn"
{
	printf '@@status|%s|127.0.0.1|origin|testhost|disk|%s|green||green|%s|0||0||%s|0|linux|/\n' \
		"$ts" $((ts+1800)) "$ts" "$ts"
	printf 'disk report\n/dev/sdd1 1000000 400000 600000 40%% %s\n' "$longmount"
	printf '@@\n'
} | env XYMONHOME="$work" XYMONTMP="$work/tmp" \
	"$XYMOND_RRD" --rrddir="$work/rrd" --no-cache 2>/dev/null

[ -e "$work/rrd/testhost/$legacyfn" ] \
	&& fail "auto-rename: long legacy name was not matched raw (shortened reconstruction misses it)"
grep -rq 'long-legacy-history' "$work/rrd/testhost" \
	|| fail "auto-rename: the long legacy file's history was not carried across"

pass "do_disk encodes mounts reversibly and migrates legacy files by auto-rename"
