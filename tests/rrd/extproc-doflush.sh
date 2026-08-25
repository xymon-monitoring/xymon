#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/rrd/extproc-doflush.sh
#
# RRD_EXTPROC_DOFLUSH must make --processor data arrive as it is produced,
# not when the buffer fills or the processor is torn down.
#
# The default is a buffered stream, so on a quiet server an update can sit
# unwritten for a long time -- fine for feeding an RRD store, not fine for a
# processor forwarding to something that expects a live stream. DOFLUSH is
# the documented escape hatch (xymond_rrd.8), and it is worth a guard: it is
# one boolean in setup_extprocessor(), and losing it would be invisible
# until an operator noticed their feed had gone quiet.
#
# The assertion is made while xymond_rrd is still running -- stdin is held
# open through a fifo -- because at teardown the buffered stream is flushed
# too (tests/rrd/extproc-teardown-flush.sh covers that path), so a run that
# waits for exit cannot tell the two modes apart.
#
# Deliberately only the positive direction. Asserting that the *default*
# has not delivered yet means asserting the absence of an event, which on a
# loaded runner is a wall-clock race, and a flaky test is worse than none
# (tests/README.md).

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin XYMOND_RRD xymond/xymond_rrd

work=$(mktempdir)
mkdir -p "$work/home/etc" "$work/tmp" "$work/rrd"
: > "$work/home/etc/analysis.cfg"
: > "$work/hosts.cfg"

cat > "$work/processor" <<EOF
#!/bin/sh
while IFS= read -r line; do printf '%s\n' "\$line" >> "$work/received.txt"; done
EOF
chmod +x "$work/processor"

mkfifo "$work/feed"

ts=$(date +%s)

# The buffer is forced far above one update, so nothing can reach the
# processor by filling it: only a flush can, which is the point.
# XYMONRUNDIR is where xymond_rrd binds its cache-control socket. Its
# fallback is XYMONLOGDIR, which resolves to the build-time install path
# and is not writable from a test tree, so the bind fails and nothing is
# delivered.
env \
	XYMONHOME="$work/home" \
	XYMONVAR="$work" \
	XYMONTMP="$work/tmp" \
	XYMONRUNDIR="$work/tmp" \
	XYMONRRDS="$work/rrd" \
	HOSTSCFG="$work/hosts.cfg" \
	TEST2RRD="inode" \
	GRAPHS="inode" \
	RRD_EXTPROC_BUFSIZ=65536 \
	RRD_EXTPROC_DOFLUSH=1 \
	"$XYMOND_RRD" --no-cache --no-rrd --rrddir="$work/rrd" \
	--processor="$work/processor" \
	<"$work/feed" >"$work/xymond_rrd.log" 2>&1 &
worker=$!
register_cleanup "kill $worker 2>/dev/null || true"

# Hold the write end open so the worker keeps reading after the message.
exec {feed}>"$work/feed"
register_cleanup "exec {feed}>&- 2>/dev/null || true"

printf '@@status|%s|127.0.0.1||unix.test|inode|%s|green||||||||||unix|\n' "$ts" "$((ts + 1800))" >&$feed
printf 'status unix.test.inode green %s - Filesystems ok\n' "$ts" >&$feed
printf 'Filesystem itotal iused ifree %%%%iused Mounted on\n' >&$feed
printf '/dev/ld0a 259070 41020 218050 15%%%% /\n@@\n' >&$feed

# Wait for the condition, not for a duration: poll until the processor has
# recorded something, with a ceiling so a broken build fails instead of
# hanging the suite.
delivered=no
for _ in $(seq 1 100); do
	if [ -s "$work/received.txt" ]; then delivered=yes; break; fi
	sleep 0.1
done

if [ "$delivered" != yes ]; then
	exec {feed}>&-
	wait "$worker" 2>/dev/null || true
	cat "$work/xymond_rrd.log" >&2
	fail "RRD_EXTPROC_DOFLUSH did not deliver the update while xymond_rrd was still running"
fi

received=$(cat "$work/received.txt")

exec {feed}>&-
wait "$worker" 2>/dev/null || true

assert_contains "unix.test inode" "$received" \
	"the flushed update names the host and test"

pass "RRD_EXTPROC_DOFLUSH delivers --processor data while xymond_rrd is still running"
