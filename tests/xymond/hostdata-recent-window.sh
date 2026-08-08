#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/xymond/hostdata-recent-window.sh
#
# Regression guard: xymond_hostdata must save client data even when the
# machine's uptime is below --recent-period.
#
# A first-seen host has calloc'ed (zero) save timestamps, and gettimer() is
# CLOCK_MONOTONIC (seconds since boot), so while uptime is below the window
# "now - recentperiod" is negative, every empty slot counts as a recent
# save, and the data is dropped unlogged.
#
# The test cannot reboot the host, but the comparison only depends on
# "now - recentperiod", so a window larger than the current uptime
# reproduces the condition exactly.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)

CC=${CC:-cc}
command -v "$CC" >/dev/null 2>&1 || skip "no C compiler available (CC=$CC)"
command -v make >/dev/null 2>&1 || skip "make not available"
[ -f "$ROOT/include/config.h" ] || skip "tree not configured (no include/config.h)"
[ -f "$ROOT/Makefile" ] || skip "tree not configured (no Makefile)"

ssllibs=$(sed -n 's/^SSLLIBS *= *//p' "$ROOT/Makefile")
pcre_libs=${PCRELIBS:-}
if [ -z "$pcre_libs" ] && command -v pkg-config >/dev/null 2>&1; then
	pcre_libs=$(pkg-config --libs libpcre2-8 2>/dev/null || true)
fi
[ -n "$pcre_libs" ] || pcre_libs="-lpcre2-8"

work=$(mktemp -d "${TMPDIR:-/tmp}/xymon-hostdata-window.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM

make -C "$ROOT/lib" libxymon.a libxymoncomm.a libxymontime.a >"$work/libbuild.log" 2>&1 \
	|| { cat "$work/libbuild.log" >&2; fail "cannot build the xymon libraries"; }

# HOSTDATAOBJS = xymond_hostdata.o xymond_worker.o (xymond/Makefile).
# Archives listed twice rather than --start-group, which is GNU ld only.
"$CC" -I"$ROOT/include" -I"$ROOT/lib" -I"$ROOT/xymond" -o "$work/xymond_hostdata" \
	"$ROOT/xymond/xymond_hostdata.c" "$ROOT/xymond/xymond_worker.c" \
	"$ROOT/lib/libxymon.a" "$ROOT/lib/libxymoncomm.a" "$ROOT/lib/libxymontime.a" \
	"$ROOT/lib/libxymon.a" \
	$pcre_libs $ssllibs 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; fail "xymond_hostdata does not build -- cannot verify the save window"; }

mkdir -p "$work/etc"
cp "$ROOT/xymond/etcfiles/xymonserver.cfg.DIST" "$work/etc/xymonserver.cfg"

save() {  # save <recent-period-minutes> -- returns 0 if the data was written
	rm -rf "$work/var"; mkdir -p "$work/var/hostdata"
	printf '@@clichg#1|1|10.0.0.99|realhost|SAVED|linux\nclient payload\n@@\n' \
	| XYMONHOME="$work" XYMONVAR="$work/var" \
		"$work/xymond_hostdata" --env="$work/etc/xymonserver.cfg" \
		--logdir="$work/var/hostdata" --minimum-free=0 \
		--recent-period="$1" >/dev/null 2>&1
	[ -f "$work/var/hostdata/realhost/SAVED" ]
}

# A window no uptime can outgrow (~19 years; 60*10000000 still fits the
# signed int used by option parsing) stands in for a freshly booted host.
save 10000000 \
	|| fail "client data dropped when the save window exceeds uptime -- a rebooted server loses hostdata until it has been up for --recent-period"

# The ordinary case must keep working: a window well under this machine's
# uptime, and the degenerate zero window.
save 60 || fail "client data not saved with the default-sized window"
save 0  || fail "client data not saved with a zero window"

echo "OK $(basename "$0")"
