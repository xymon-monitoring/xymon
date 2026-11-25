#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/df-longmount-line.sh
#
# unix_disk_report() and unix_inode_report() build each filesystem's alert
# line in a fixed char msgline[4096]. The mount point in that line comes
# verbatim from the client's df output and has no length limit, so a mount
# point longer than the buffer must be bounded rather than written past its
# end -- and bounded in a way that keeps each line whole, with its trailing
# newline and, for the inode line, its closing "-->".
#
# Pinned here by driving the real xymond_client in --local --no-update mode
# (statuses print to stdout instead of being sent). Four things:
#
#   1. a mount point far longer than the buffer, at a level that fires the
#      threshold, is handled without trouble: the worker exits 0 and still
#      emits the disk and inode status;
#   2. a short mount after the long one still gets its own whole line -- its
#      alert begins a line, which it cannot if the long line lost its newline;
#   3. the inode line's HTML comment is closed: every "<!-- ID=" is matched by
#      a "-->", so a truncated mount point cannot leave a comment open and
#      swallow the rest of the status page;
#   4. the units-rule path (a rule in units, a report with no free column, the
#      "... is checked in units free ..." line that also carries the mount
#      point) is bounded too, not only the percentage path.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin XYMOND_CLIENT xymond/xymond_client

work=$(mktempdir)
mkdir -p "$work/home"

# A mount point well past msgline[4096].
long="/mnt/$(awk 'BEGIN { while (n++ < 5000) printf "a" }')"

runclient() {	# $1 = config file; report on stdin; prints the status output
	env XYMONHOME="$work/home" XYMONTMP="$work" \
		"$XYMOND_CLIENT" --local --no-update --config="$1" 2>"$work/stderr.log"
}

# Every "<!-- ID=" must have a matching "-->", or a truncated mount point has
# left an HTML comment open.
assert_comments_closed() {	# $1 = output, $2 = message
	local o c
	o=$(grep -o '<!-- ID=' <<<"$1" | wc -l | tr -d ' ' || true)
	c=$(grep -o -- '-->' <<<"$1" | wc -l | tr -d ' ' || true)
	[ "$o" = "$c" ] || fail "$2 (open '<!-- ID=' $o, close '-->' $c)"
}

# No alert line may have lost its trailing newline: every "&red"/"&yellow"
# must begin a line, so the count of them equals the count that start a line.
# If a long line were cut mid-way, the next alert would be appended to it and
# not begin a line. Build-independent: it does not depend on which rule fires.
assert_alert_lines_whole() {	# $1 = output, $2 = message
	local occ atstart
	occ=$(grep -oE '&(red|yellow)' <<<"$1" | wc -l | tr -d ' ' || true)
	atstart=$(grep -cE '^&(red|yellow)' <<<"$1" || true)
	[ "$occ" = "$atstart" ] \
		|| fail "$2 (&red/&yellow seen $occ, begin a line $atstart)"
}

# --- Case A: percentage rules, long mount then a short one --------------------
: > "$work/a.cfg"
reportA="@@client|1755640000|127.0.0.1|longmount.test|linux|linux|
[df]
Filesystem 1024-blocks Used Available Capacity Mounted on
/dev/sda1 1000000 1000000 0 100% ${long}
/dev/sdb1 1000000 1000000 0 100% /short
[inode]
Filesystem Inodes IUsed IFree IUse% Mounted on
/dev/sda1 65536 65536 0 100% ${long}
/dev/sdb1 65536 65536 0 100% /short
@@
"
outA=$(printf '%s' "$reportA" | runclient "$work/a.cfg") \
	|| fail "xymond_client exited non-zero on a long mount point: $(cat "$work/stderr.log")"

assert_match "longmount,test\.disk red" "$outA" \
	"no disk status was emitted for a report carrying an over-long mount point"
assert_match "longmount,test\.inode red" "$outA" \
	"no inode status was emitted for a report carrying an over-long mount point"

# The short mount's alert must START a line: if the long line had lost its
# newline, this alert would be appended to it and not match a line anchor.
grep -q '^&red /short (100% used) has reached the PANIC level' <<<"$outA" \
	|| fail "the short mount's alert does not begin its own line -- the long line lost its newline"

assert_alert_lines_whole "$outA" "an alert line lost its newline (percentage path)"
assert_comments_closed "$outA" "the inode status left an HTML comment open"

# --- Case B: an absolute (units) rule, a report with no free column -----------
# This drives the "... is checked in units free, and the report has no ..."
# line, the other msgline writer that carries the mount point. Whether that
# exact line fires depends on how the build resolves an absolute threshold for
# the report's columns, so the invariant checked here is the one that must hold
# either way: the worker survives, every alert line is whole, and no HTML
# comment is left open -- with the long mount point bounded on whichever line
# the rule produces. Where the units line does appear, it is checked too.
printf 'HOST=longmount.test\n\tINODE * 1000U 500U\n\tDISK * 1000U 500U\n' > "$work/b.cfg"
reportB="@@client|1755640000|127.0.0.1|longmount.test|linux|linux|
[df]
Filesystem 1024-blocks Used Capacity Mounted on
/dev/sda1 1000000 1000000 100% ${long}
[inode]
Filesystem Inodes IUsed IUse% Mounted on
/dev/sda1 65536 65536 100% ${long}
@@
"
outB=$(printf '%s' "$reportB" | runclient "$work/b.cfg") \
	|| fail "xymond_client exited non-zero on the units-rule path: $(cat "$work/stderr.log")"

assert_match "longmount,test\.disk" "$outB" \
	"no disk status was emitted for the units-rule report"
assert_alert_lines_whole "$outB" "an alert line lost its newline (units path)"
assert_comments_closed "$outB" "the units-rule inode line left an HTML comment open"

# Where the build does emit the units line, it must be whole (begin a line).
if grep -q 'is checked in units free, and the report has no' <<<"$outB"; then
	grep -q '^&yellow .*is checked in units free, and the report has no' <<<"$outB" \
		|| fail "the units-free line is present but did not begin its own line"
fi

pass "a long mount point is bounded on every per-filesystem disk and inode line, each kept whole, and the worker keeps going"
