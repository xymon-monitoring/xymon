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
# Both directions are checked, and the negative one needs a word, because an
# earlier version of this file left it out on the grounds that asserting the
# absence of an event is a wall-clock race. It is not one here. do_rrd.c makes
# the stream fully buffered in setup_extprocessor() and flushes it in exactly
# one place, guarded by that boolean -- so with DOFLUSH unset, one small update
# against RRD_EXTPROC_BUFSIZ=65536 *cannot* reach the processor before the
# stream closes. Load does not flush a stdio buffer; it can only delay the
# write. So the error this test can make is leniency, not flakiness: on a
# machine slow enough that the write has not happened yet, the absence check
# passes for the wrong reason. That is why the default case also asserts the
# data arrives after teardown -- an update that never appeared at all fails the
# test rather than quietly satisfying it.

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
while IFS= read -r line; do printf '%s\n' "\$line" >> "\$RECEIVED"; done
EOF
chmod +x "$work/processor"

ts=$(date +%s)

# feed_one CASE DOFLUSH -- run a fresh xymond_rrd, send one update, and leave
# it running with the fifo's write end still open. Sets:
#   received  the file the processor appends to
#   worker    its pid
# Explicit descriptor 9 rather than "exec {fd}>": automatic allocation is bash
# 4.1, and the suite's floor is the 3.2 macOS ships (tests/README.md).
feed_one() {
	fo_case=$1 fo_doflush=$2
	received="$work/received-$fo_case.txt"
	: > "$received"
	mkfifo "$work/feed-$fo_case"

	# The buffer is forced far above one update, so nothing can reach the
	# processor by filling it: only a flush can, which is the point.
	env \
		XYMONHOME="$work/home" \
		XYMONVAR="$work" \
		XYMONTMP="$work/tmp" \
		XYMONRRDS="$work/rrd" \
		HOSTSCFG="$work/hosts.cfg" \
		TEST2RRD="inode" \
		GRAPHS="inode" \
		RECEIVED="$received" \
		RRD_EXTPROC_BUFSIZ=65536 \
		${fo_doflush:+RRD_EXTPROC_DOFLUSH=$fo_doflush} \
		"$XYMOND_RRD" --no-cache --no-rrd --rrddir="$work/rrd" \
		--processor="$work/processor" \
		<"$work/feed-$fo_case" >"$work/xymond_rrd-$fo_case.log" 2>&1 &
	worker=$!
	register_cleanup "kill $worker 2>/dev/null || true"

	# Hold the write end open so the worker keeps reading after the message.
	exec 9>"$work/feed-$fo_case"
	register_cleanup "exec 9>&- 2>/dev/null || true"

	printf '@@status|%s|127.0.0.1||unix.test|inode|%s|green||||||||||unix|\n' "$ts" "$((ts + 1800))" >&9
	printf 'status unix.test.inode green %s - Filesystems ok\n' "$ts" >&9
	printf 'Filesystem itotal iused ifree %%%%iused Mounted on\n' >&9
	printf '/dev/ld0a 259070 41020 218050 15%%%% /\n@@\n' >&9
}

teardown_one() {
	exec 9>&-
	wait "$worker" 2>/dev/null || true
}

# ---- DOFLUSH=1: the update arrives while the daemon is still running --------
feed_one flush 1

# Wait for the condition, not for a duration: poll until the processor has
# recorded something, with a ceiling so a broken build fails instead of
# hanging the suite.
delivered=no
polls=0
for _ in $(seq 1 100); do
	if [ -s "$received" ]; then delivered=yes; break; fi
	polls=$((polls + 1))
	sleep 0.1
done

if [ "$delivered" != yes ]; then
	teardown_one
	cat "$work/xymond_rrd-flush.log" >&2
	fail "RRD_EXTPROC_DOFLUSH did not deliver the update while xymond_rrd was still running"
fi

flushed=$(cat "$received")
teardown_one

assert_contains "unix.test inode" "$flushed" \
	"the flushed update names the host and test"

# ---- the default: nothing until the stream closes ---------------------------
# Without this, the pair of assertions cannot tell "DOFLUSH works" from
# "buffering was never added": removing the buffering feature outright would
# also deliver live, and the test above would still pass.
feed_one default ""

# Calibrated against the flushed case rather than a fixed ceiling: the default
# gets several times however long a live delivery actually took on this machine,
# so a slow runner stretches both and a fast one does not pay ten seconds for one
# assertion. A floor keeps it meaningful when the flushed case was instant.
ceiling=$(( (polls + 1) * 5 ))
[ "$ceiling" -ge 10 ] || ceiling=10
for _ in $(seq 1 "$ceiling"); do
	[ -s "$received" ] && break
	sleep 0.1
done

if [ -s "$received" ]; then
	early=$(cat "$received")
	teardown_one
	cat "$work/xymond_rrd-default.log" >&2
	fail "the default delivered to the processor while xymond_rrd was still running, so the stream is not buffered and DOFLUSH changes nothing: $early"
fi

teardown_one

# The control on the check above: the update must exist and reach the processor
# at teardown. A run that produced nothing at all would otherwise satisfy the
# absence assertion without proving anything about buffering.
assert_file_exists "$received" \
	"the default lost the update entirely instead of buffering it"
assert_contains "unix.test inode" "$(cat "$received")" \
	"the buffered update did not reach the processor when the stream closed"

pass "RRD_EXTPROC_DOFLUSH delivers while xymond_rrd runs, and the default holds the update until the stream closes"
