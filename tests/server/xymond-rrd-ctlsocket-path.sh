#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/xymond-rrd-ctlsocket-path.sh
#
# Regression guard for issue #236: xymond_rrd composed its cache-control
# socket path with an unchecked sprintf into sun_path (~108 bytes), so any
# XYMONTMP longer than ~92 characters overflowed the buffer and aborted the
# daemon at startup ("*** buffer overflow detected ***") with no logged
# cause. It must instead refuse to start with a clear error naming the
# problem - and keep starting normally with an ordinary XYMONTMP.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin XYMOND_RRD "xymond/xymond_rrd"

work=$(mktempdir)

# A path safely past sun_path (108 bytes on Linux, as small as 92 elsewhere)
longtmp="$work/$(printf 'x%.0s' $(seq 1 120))"
mkdir -p "$longtmp" "$work/rrd" "$work/tmp"

# Overlong XYMONTMP: a clean refusal (exit 1 + message), not a SIGABRT (134)
rc=0
out=$(echo -n | XYMONTMP="$longtmp" XYMONHOME="$work" \
	"$XYMOND_RRD" --rrddir="$work/rrd" --no-cache 2>&1) || rc=$?
[ "$rc" -eq 1 ] || fail "expected clean exit 1 on overlong XYMONTMP, got $rc: $out"
assert_contains "XYMONTMP is too long" "$out" "overlong XYMONTMP refused with a clear error"

# An ordinary XYMONTMP still starts and shuts down cleanly on EOF
rc=0
out=$(echo -n | XYMONTMP="$work/tmp" XYMONHOME="$work" \
	"$XYMOND_RRD" --rrddir="$work/rrd" --no-cache 2>&1) || rc=$?
[ "$rc" -eq 0 ] || fail "expected clean exit 0 with short XYMONTMP, got $rc: $out"

pass "xymond_rrd refuses an overlong XYMONTMP instead of aborting"
