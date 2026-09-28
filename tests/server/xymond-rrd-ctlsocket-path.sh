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

# Print the name of the rrdctl.* socket that xymond_rrd binds in DIR, waiting
# up to ten seconds for it to appear.
wait_for_socket() {
	local s
	for _ in $(seq 1 100); do
		s=$(ls "$1" 2>/dev/null | grep '^rrdctl\.' | head -1) || true
		[ -n "$s" ] && { printf '%s\n' "$s"; return 0; }
		sleep 0.1
	done
	return 1
}

mkdir -p "$work/run"
# The sleep keeps the daemon's input open. Killing the daemon leaves it running out
# its 30 seconds, so it must not hold the test's stderr: whoever reads this
# test's output would wait for it.
sleep 30 2>/dev/null | XYMONRUNDIR="$work/run" XYMONTMP="$work/tmp" XYMONHOME="$work" \
	"$XYMOND_RRD" --rrddir="$work/rrd" >"$work/rrd.log" 2>&1 &
rrdpid=$!
register_cleanup "kill $rrdpid 2>/dev/null || true"

sock=$(wait_for_socket "$work/run") || fail "xymond_rrd did not create its control socket in XYMONRUNDIR: $(cat "$work/rrd.log")"

# stdin from /dev/null: rrdcachectl reads hostnames to flush until end of input.
rc=0
out=$(XYMONRUNDIR="$work/run" XYMONTMP="$work/tmp" XYMONHOME="$work" \
	"$RRDCACHECTL" "$sock" </dev/null 2>&1) || rc=$?
[ "$rc" -eq 0 ] \
	|| fail "rrdcachectl could not reach the socket xymond_rrd bound in XYMONRUNDIR (rc=$rc): $out"

kill "$rrdpid" 2>/dev/null || true

# ---- an empty XYMONRUNDIR is an unset one -----------------------------------
# xgetenv() applies its default only to a missing variable, so a config line
# XYMONRUNDIR="" composed "/rrdctl.<pid>" -- a socket at the filesystem root,
# created there when run as root. xymond_rrd with it empty, and rrdcachectl
# with it unset, must both fall back to the log directory instead.
mkdir -p "$work/logs"
sleep 30 2>/dev/null | XYMONRUNDIR="" XYMONSERVERLOGS="$work/logs" XYMONTMP="$work/tmp" XYMONHOME="$work" \
	"$XYMOND_RRD" --rrddir="$work/rrd" >"$work/rrd-empty.log" 2>&1 &
emptypid=$!
register_cleanup "kill $emptypid 2>/dev/null || true"

sock=$(wait_for_socket "$work/logs") || fail "with XYMONRUNDIR empty, xymond_rrd did not bind in XYMONSERVERLOGS: $(cat "$work/rrd-empty.log")"

rc=0
out=$( unset XYMONRUNDIR; XYMONSERVERLOGS="$work/logs" XYMONTMP="$work/tmp" XYMONHOME="$work" \
	"$RRDCACHECTL" "$sock" </dev/null 2>&1 ) || rc=$?
[ "$rc" -eq 0 ] \
	|| fail "with XYMONRUNDIR unset, rrdcachectl did not find the socket in XYMONSERVERLOGS (rc=$rc): $out"
kill "$emptypid" 2>/dev/null || true

# A directory too long for sun_path is refused, not truncated into a
# path that names some other socket.
rc=0
out=$(XYMONRUNDIR="$longtmp" "$RRDCACHECTL" "$sock" </dev/null 2>&1) || rc=$?
[ "$rc" -eq 1 ] || fail "expected exit 1 from rrdcachectl with an overlong XYMONRUNDIR, got $rc: $out"
assert_contains "Socket path does not fit" "$out" "rrdcachectl refuses an overlong socket path"

# and with XYMONRUNDIR unset rather than empty, the daemon lands in the same
# place, XYMONSERVERLOGS, not the compiled log directory.
mkdir -p "$work/logs2"
# sh -c unsets and then execs, so $! is the daemon itself and the kill below
# reaches it -- a ( unset; ... ) & subshell would leave $! naming the subshell.
sleep 30 2>/dev/null | XYMONSERVERLOGS="$work/logs2" XYMONTMP="$work/tmp" XYMONHOME="$work" \
	sh -c 'unset XYMONRUNDIR; exec "$0" --rrddir="$1"' "$XYMOND_RRD" "$work/rrd" \
	>"$work/rrd-unset.log" 2>&1 &
unsetpid=$!
register_cleanup "kill $unsetpid 2>/dev/null || true"

sock=$(wait_for_socket "$work/logs2") || fail "with XYMONRUNDIR unset, xymond_rrd did not bind in XYMONSERVERLOGS: $(cat "$work/rrd-unset.log")"
kill "$unsetpid" 2>/dev/null || true

# ---- a missing XYMONRUNDIR is refused, naming the directory ------------------
# xymond_rrd creates one missing level and no more. Beyond that the bind fails,
# and the message has to say which setting to fix: the socket path alone does
# not.
rc=0
out=$(echo -n | XYMONRUNDIR="$work/absent/deeper" XYMONHOME="$work" \
	"$XYMOND_RRD" --rrddir="$work/rrd" --no-cache 2>&1) || rc=$?
[ "$rc" -eq 1 ] || fail "expected exit 1 with a missing XYMONRUNDIR, got $rc: $out"
assert_contains "in XYMONRUNDIR ($work/absent/deeper)" "$out" "a missing XYMONRUNDIR is named in the error"

# One missing level is created: /run/xymon is gone after a reboot, and
# xymond_rrd must not need it made first.
rc=0
out=$(echo -n | XYMONRUNDIR="$work/fresh" XYMONHOME="$work" \
	"$XYMOND_RRD" --rrddir="$work/rrd" --no-cache 2>&1) || rc=$?
[ "$rc" -eq 0 ] || fail "xymond_rrd did not start with XYMONRUNDIR one missing level down (rc=$rc): $out"
[ -d "$work/fresh" ] || fail "xymond_rrd started but did not create XYMONRUNDIR $work/fresh"

# ---- the boundary: a path of exactly sizeof(sun_path) -------------------------
# It leaves no room for the terminator, so rrdcachectl refuses it; one byte
# less is accepted, and then fails only because no socket is there. The paths
# need not exist: the length is checked first.
require_cc
"$CC" -o "$work/sun-path-size" "$(dirname "$0")/../lib/sun-path-size.c" 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; fail "sun-path-size does not compile"; }
size=$("$work/sun-path-size")
# "/" + k x's + "/rrdctl.1" is k + 10 characters.
at=$(printf 'x%.0s' $(seq 1 $((size - 10))))
below=$(printf 'x%.0s' $(seq 1 $((size - 11))))
out=$(XYMONRUNDIR="/$at" "$RRDCACHECTL" rrdctl.1 </dev/null 2>&1) || :
assert_contains "Socket path does not fit" "$out" "rrdcachectl refuses a path of exactly sizeof(sun_path) ($size)"
out=$(XYMONRUNDIR="/$below" "$RRDCACHECTL" rrdctl.1 </dev/null 2>&1) || :
assert_not_contains "Socket path does not fit" "$out" "rrdcachectl accepts a path one byte under sizeof(sun_path)"

pass "xymond_rrd and rrdcachectl agree on the socket in XYMONRUNDIR and bound its path"
