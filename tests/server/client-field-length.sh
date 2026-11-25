#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/client-field-length.sh
#
# xymond_client and client_config build their status and alert lines in fixed
# buffers (msgline[4096], msgline[PATH_MAX], sectionname[PATH_MAX],
# msgline[MAX_LINE_LEN]). Several of the fields written into those lines come
# verbatim from the client message and have no length limit: a filesystem's
# mount point, the host name, the [date] text, and a [msgs:]/[file:]/[dir:]/
# [logfile:] section name. Under --local a client message may be tens of MB,
# so each of those must be written with a bound.
#
# This drives the real xymond_client in --local --no-update mode (statuses go
# to stdout) with an over-long value in each of those fields in turn, and
# asserts on the emitted content rather than only on the exit status, so a
# regression shows up on any build, whatever its compiler's own checks happen
# to catch:
#
#   - the worker completes and still emits the status for that report;
#   - the output stays well formed: every "&red"/"&yellow" alert begins a line
#     (so no line lost its newline), and every "<!-- ID=" is closed by a
#     "-->" (so a cut cannot leave an HTML comment open and swallow the page);
#   - the per-filesystem lines carry the whole mount point, and a short mount
#     after a long one still gets a line of its own.
#
# A normal report is checked too, so the bounding cannot quietly change the
# text of an ordinary alert.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin XYMOND_CLIENT xymond/xymond_client

work=$(mktempdir)
mkdir -p "$work/home"

long=$(awk 'BEGIN { while (n++ < 6000) printf "a" }')

printf 'HOST=fl.test\n\tDISK * 90 95\n\tINODE * 90 95\n' > "$work/pct.cfg"
printf 'HOST=fl.test\n\tDISK * 1000U 500U\n\tINODE * 1000U 500U\n' > "$work/units.cfg"

runclient() {	# $1 = config; report on stdin
	env XYMONHOME="$work/home" XYMONTMP="$work" \
		"$XYMOND_CLIENT" --local --no-update --config="$1" 2>"$work/stderr.log"
}

# Every "&red"/"&yellow" must begin a line: if a line had been cut short of
# its newline, the next alert would be appended to it instead.
assert_alerts_whole() {	# $1 = output, $2 = what was being checked
	local occ atstart
	occ=$(grep -oE '&(red|yellow)' <<<"$1" | wc -l | tr -d ' ' || true)
	atstart=$(grep -cE '^&(red|yellow)' <<<"$1" || true)
	[ "$occ" = "$atstart" ] \
		|| fail "$2: an alert line lost its newline (&red/&yellow seen $occ, begin a line $atstart)"
}

# Every "<!-- ID=" must be closed, or a cut left an HTML comment open.
assert_comments_closed() {	# $1 = output, $2 = what was being checked
	local o c
	o=$(grep -o '<!-- ID=' <<<"$1" | wc -l | tr -d ' ' || true)
	c=$(grep -o -- '-->' <<<"$1" | wc -l | tr -d ' ' || true)
	[ "$o" = "$c" ] \
		|| fail "$2: an HTML comment was left open (open $o, closed $c)"
}

# check_field NAME CONFIG REPORT EXPECTED-STATUS-RE
check_field() {
	local name=$1 cfg=$2 report=$3 want=$4 out
	# The report is held in a variable, so its trailing newline is gone:
	# put it back, or the closing "@@" does not end the message.
	out=$(printf '%s\n' "$report" | runclient "$work/$cfg.cfg") \
		|| fail "$name: xymond_client exited non-zero: $(tail -n 3 "$work/stderr.log")"
	assert_match "$want" "$out" "$name: no status was emitted"
	assert_alerts_whole "$out" "$name"
	assert_comments_closed "$out" "$name"
}

hdr='@@client|1755640000|127.0.0.1|fl.test|linux|linux|'

# --- an over-long mount point, in both the disk and the inode report ---------
# Also checks the whole mount point survives and a short mount keeps its line.
out=$(printf '%s\n[df]\nFilesystem 1024-blocks Used Available Capacity Mounted on\n/dev/sda1 100 100 0 100%% /m/%s\n/dev/sdb1 100 100 0 100%% /short\n[inode]\nFilesystem Inodes IUsed IFree IUse%% Mounted on\n/dev/sda1 100 100 0 100%% /m/%s\n/dev/sdb1 100 100 0 100%% /short\n@@\n' \
		"$hdr" "$long" "$long" | runclient "$work/pct.cfg") \
	|| fail "mount point: xymond_client exited non-zero: $(tail -n 3 "$work/stderr.log")"
assert_match "fl,test\.disk red" "$out" "mount point: no disk status was emitted"
assert_match "fl,test\.inode red" "$out" "mount point: no inode status was emitted"
assert_alerts_whole "$out" "mount point"
assert_comments_closed "$out" "mount point"
grep -q "^&red /m/${long} (100% used) has reached the PANIC level" <<<"$out" \
	|| fail "mount point: the alert does not carry the whole mount point on a line of its own"
grep -q '^&red /short (100% used) has reached the PANIC level' <<<"$out" \
	|| fail "mount point: the short mount after the long one lost its own line"

# The same, with an absolute rule and a report that has no free column: this
# is the "... is checked in units free ..." line, which also carries the name.
check_field "mount point, units rule" units \
	"$(printf '%s\n[df]\nFilesystem 1024-blocks Used Capacity Mounted on\n/dev/sda1 100 100 100%% /m/%s\n[inode]\nFilesystem Inodes IUsed IUse%% Mounted on\n/dev/sda1 100 100 100%% /m/%s\n@@\n' "$hdr" "$long" "$long")" \
	"fl,test\.disk"

# --- an over-long host name (accepted as-is under --local) -------------------
# The host name opens the status line, so a name longer than the buffer is
# what gets truncated: the line carries the name up to the bound and the
# ".disk" suffix is cut. The point here is that the write stays in bounds and
# the worker carries on, not that the status name survives.
check_field "host name" pct \
	"$(printf '@@client|1755640000|127.0.0.1|%s|linux|linux|\n[df]\nFilesystem 1024-blocks Used Available Capacity Mounted on\n/dev/sda1 100 50 50 50%% /\n@@\n' "$long")" \
	"aaaaaaaaaa"

# --- an over-long [date] ----------------------------------------------------
check_field "[date]" pct \
	"$(printf '%s\n[date]\n%s\n[uptime]\n 10:00:00 up 5 days, load average: 0.1, 0.2, 0.3\n@@\n' "$hdr" "$long")" \
	"fl,test\."

# --- over-long section names ------------------------------------------------
check_field "[msgs:] section" pct \
	"$(printf '%s\n[msgs:/var/log/%s]\nNov 25 09:00:00 fl kernel: x\n@@\n' "$hdr" "$long")" \
	"fl,test\.msgs"
check_field "[file:] section" pct \
	"$(printf '%s\n[file:/etc/%s]\ntype:file\nsize:1\nmode:644\n@@\n' "$hdr" "$long")" \
	"fl,test\.files"
check_field "[dir:] section" pct \
	"$(printf '%s\n[dir:/var/%s]\n1024 /var/x\n@@\n' "$hdr" "$long")" \
	"fl,test\.files"
check_field "[logfile:] section" pct \
	"$(printf '%s\n[logfile:/var/log/%s]\nNov 25 x\n@@\n' "$hdr" "$long")" \
	"fl,test\."

# --- an over-long load= value ----------------------------------------------
# The load= path copies the value into loadresult[100] for the status line.
# The value must be numeric: strspn(lstart, "0123456789.") is what advances
# the pointer, so only digits and dots reach the copy.
longnum=$(awk 'BEGIN { while (n++ < 6000) printf "1" }')
check_field "load= value" pct \
	"$(printf '%s\n[uptime]\n 10:00:00 up 5 days, load=%s\n@@\n' "$hdr" "$longnum")" \
	"fl,test\.cpu"

# --- an ordinary report: the alert text must not have moved -----------------
out=$(printf '%s\n[date]\nWed Nov 25 10:00:00 UTC 2026\n[df]\nFilesystem 1024-blocks Used Available Capacity Mounted on\n/dev/sda1 100 100 0 100%% /full\n/dev/sdb1 100 5 95 5%% /ok\n[inode]\nFilesystem Inodes IUsed IFree IUse%% Mounted on\n/dev/sda1 100 100 0 100%% /full\n@@\n' \
		"$hdr" | runclient "$work/pct.cfg") \
	|| fail "ordinary report: xymond_client exited non-zero: $(tail -n 3 "$work/stderr.log")"
assert_contains "&red /full (100% used) has reached the PANIC level (95%)" "$out" \
	"an ordinary disk alert's text changed"
assert_contains "<!-- ID=/full --> /full (100% used) has reached the PANIC level (95%)" "$out" \
	"an ordinary inode alert's text changed"
assert_contains "Wed Nov 25 10:00:00 UTC 2026" "$out" \
	"an ordinary [date] no longer reaches the status line"
assert_alerts_whole "$out" "ordinary report"
assert_comments_closed "$out" "ordinary report"

pass "every over-long client field is written with a bound, the output stays well formed, and ordinary alerts are unchanged"
