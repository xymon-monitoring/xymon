#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/rrd/extproc-teardown-flush.sh
#
# Regression guard: everything written to the --processor stream must reach
# the external processor, including whatever is still sitting in the stdio
# buffer when xymond_rrd shuts the processor down.
#
# xymond_rrd buffers that stream (RRD_EXTPROC_BUFSIZ, default BUFSIZ), so on
# a quiet server an update can sit unwritten indefinitely. Teardown is what
# gets it out: shutdown_extprocessor() runs on exit and whenever the
# processor is restarted after a SIGPIPE. It used to close(2) the raw fd and
# NULL the FILE* without fclose(), so the buffer was discarded rather than
# flushed -- silently, since close() succeeds. Every update since the last
# buffer-fill was lost on each restart and on every shutdown.
#
# The buffer size is forced well above one update so the line cannot reach
# the processor by filling the buffer; only the teardown flush can deliver
# it, which is exactly the behaviour under test.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin XYMOND_RRD xymond/xymond_rrd

work=$(mktempdir)
mkdir -p "$work/home/etc" "$work/tmp" "$work/rrd"
: > "$work/home/etc/analysis.cfg"
: > "$work/hosts.cfg"

# The processor records each line as it arrives, so "what got through" is
# readable after the run. Line-buffered by read/printf, not by stdio, so a
# partial delivery would still show up.
cat > "$work/processor" <<EOF
#!/bin/sh
while IFS= read -r line; do printf '%s\n' "\$line" >> "$work/received.txt"; done
EOF
chmod +x "$work/processor"

ts=$(date +%s)
message="@@status|$ts|127.0.0.1||unix.test|inode|$((ts + 1800))|green||||||||||unix|
status unix.test.inode green $ts - Filesystems ok
Filesystem itotal iused ifree %iused Mounted on
/dev/ld0a 259070 41020 218050 15% /
@@
"

# --no-rrd: this test is about the processor stream, not the RRD files.
printf '%s' "$message" | # XYMONRUNDIR is where xymond_rrd binds its cache-control socket. Its
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
	"$XYMOND_RRD" --no-cache --no-rrd --rrddir="$work/rrd" \
	--processor="$work/processor" \
	>"$work/xymond_rrd.log" 2>&1 \
	|| { cat "$work/xymond_rrd.log" >&2; fail "xymond_rrd exited non-zero"; }

# Distinguish "the update never reached the stream" from "it reached the
# stream and teardown dropped it" -- without this the assertion below could
# pass vacuously if the message stopped being parsed at all.
grep -q "External processor '$work/processor' started" "$work/xymond_rrd.log" \
	|| { cat "$work/xymond_rrd.log" >&2; fail "the external processor was never started"; }

# The processor is forked by xymond_rrd, not by this shell, so there is no
# handle to wait on: when xymond_rrd exits, the flushed bytes are in the pipe
# but the processor may not have been scheduled to read them yet. Wait for the
# condition, not for a duration -- with the fclose() reverted the file never
# appears, so the ceiling only bounds the failing run.
delivered=no
for _ in $(seq 1 100); do
	if [ -s "$work/received.txt" ]; then delivered=yes; break; fi
	sleep 0.1
done

if [ "$delivered" != yes ]; then
	cat "$work/xymond_rrd.log" >&2
	fail "the external processor received nothing: the buffered update was discarded at teardown instead of flushed"
fi

received=$(cat "$work/received.txt")
assert_contains "unix.test inode" "$received" \
	"the update that reached the external processor names the host and test"

pass "buffered --processor data is flushed to the processor at teardown"
