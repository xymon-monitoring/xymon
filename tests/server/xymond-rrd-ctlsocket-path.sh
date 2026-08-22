#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/xymond-rrd-ctlsocket-path.sh
#
# Regression guard for issue #236: xymond_rrd composed its cache-control
# socket path with an unchecked sprintf into sun_path (~108 bytes), so any
# socket directory longer than ~92 characters overflowed the buffer and
# aborted the daemon at startup ("*** buffer overflow detected ***") with no
# logged cause. It must instead refuse to start with a clear error naming
# the problem - and keep starting normally with an ordinary directory.
# The socket directory is XYMONRUNDIR (it was XYMONTMP when #236 was fixed).

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin XYMOND_RRD "xymond/xymond_rrd"

work=$(mktempdir)

# A path safely past sun_path (108 bytes on Linux, as small as 92 elsewhere)
longtmp="$work/$(printf 'x%.0s' $(seq 1 120))"
mkdir -p "$longtmp" "$work/rrd" "$work/tmp"

# Overlong XYMONRUNDIR: a clean refusal (exit 1 + message), not a SIGABRT (134)
rc=0
out=$(echo -n | XYMONRUNDIR="$longtmp" XYMONHOME="$work" \
	"$XYMOND_RRD" --rrddir="$work/rrd" --no-cache 2>&1) || rc=$?
[ "$rc" -eq 1 ] || fail "expected clean exit 1 on overlong XYMONRUNDIR, got $rc: $out"
assert_contains "XYMONRUNDIR is too long" "$out" "overlong XYMONRUNDIR refused with a clear error"

# An ordinary XYMONRUNDIR still starts and shuts down cleanly on EOF
rc=0
out=$(echo -n | XYMONRUNDIR="$work/tmp" XYMONHOME="$work" \
	"$XYMOND_RRD" --rrddir="$work/rrd" --no-cache 2>&1) || rc=$?
[ "$rc" -eq 0 ] || fail "expected clean exit 0 with short XYMONRUNDIR, got $rc: $out"

# ---- the two ends must agree on the directory -------------------------------
# xymond_rrd binds its control socket under XYMONRUNDIR; rrdcachectl connects
# to it by name and composes the directory itself. It was still composing from
# XYMONTMP, so the moment the two differ -- which is the whole point of having
# XYMONRUNDIR -- the flush utility looked in an empty directory and could not
# reach a running daemon.
require_bin RRDCACHECTL "xymond/rrdcachectl"

mkdir -p "$work/run"
sleep 30 | XYMONRUNDIR="$work/run" XYMONTMP="$work/tmp" XYMONHOME="$work" \
	"$XYMOND_RRD" --rrddir="$work/rrd" >"$work/rrd.log" 2>&1 &
rrdpid=$!
register_cleanup "kill $rrdpid 2>/dev/null || true"

sock=""
for _ in $(seq 1 100); do
	sock=$(ls "$work/run" 2>/dev/null | grep '^rrdctl\.' | head -1) && [ -n "$sock" ] && break
	sleep 0.1
done
[ -n "$sock" ] || fail "xymond_rrd did not create its control socket in XYMONRUNDIR: $(cat "$work/rrd.log")"

# stdin from /dev/null: it reads hostnames to flush until end of input, and the
# suite runner keeps its list of tests on stdin -- a test that reads it eats the
# rest of the run, which then reports success for the tests it never ran.
rc=0
out=$(XYMONRUNDIR="$work/run" XYMONTMP="$work/tmp" XYMONHOME="$work" \
	"$RRDCACHECTL" "$sock" </dev/null 2>&1) || rc=$?
[ "$rc" -eq 0 ] \
	|| fail "rrdcachectl could not reach the socket xymond_rrd bound in XYMONRUNDIR (rc=$rc): $out"

pass "xymond_rrd refuses an overlong XYMONRUNDIR, and rrdcachectl finds the socket it binds"
