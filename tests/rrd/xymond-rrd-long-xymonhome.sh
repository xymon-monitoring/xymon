#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/rrd/xymond-rrd-long-xymonhome.sh
#
# xymond_rrd reports XYMONHOME/etc/rrddefinitions.cfg as too long for its
# buffer, and does not crash on it (#643). It built the name with sprintf()
# into a PATH_MAX buffer; a long enough XYMONHOME overflowed it, and glibc
# aborted the program as it started.
#
# Checked with a 5000-character XYMONHOME and nothing on its input: it logs
# the name as too long and exits 0 at the end of its input. XYMONTMP stays
# short, since a long one is refused earlier, for the control socket
# (xymond-rrd-ctlsocket-path.sh).

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin XYMOND_RRD xymond/xymond_rrd

work=$(mktempdir)
mkdir -p "$work/tmp" "$work/rrd"
long=$(printf '%5000s' '' | tr ' ' A)

rc=0
(cd "$work" && env XYMONHOME="$long" XYMONTMP="$work/tmp" XYMONRUNDIR="$work/tmp" \
	"$XYMOND_RRD" --rrddir="$work/rrd" </dev/null) >/dev/null 2>"$work/err" || rc=$?
assert_equal "0" "$rc" "xymond_rrd did not survive a 5000-character XYMONHOME: $(tail -c 300 "$work/err")"
assert_contains "rrddefinitions.cfg: the name is too long" "$(cat "$work/err")" \
	"xymond_rrd did not report the rrddefinitions.cfg name as too long"

pass "xymond_rrd reports an rrddefinitions.cfg name too long for its buffer, and does not crash"
