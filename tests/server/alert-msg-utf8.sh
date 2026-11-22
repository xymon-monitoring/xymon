#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/alert-msg-utf8.sh
#
# A SCRIPT alert passes the message to its paging script in $BBALPHAMSG, cut to
# MAXMSG_ALERTSCRIPT bytes. The cut must not split a multi-byte UTF-8 character:
# a script, or whatever it forwards the page to, is then handed a byte that is
# not a whole character. do_alert.c backs up over a trailing continuation byte,
# as the ack-table cut does (do_acklog).
#
# The alert message is a long run of one multi-byte character. The harness sends it through
# the real send_alert() to a SCRIPT recipient that records $BBALPHAMSG. Across a
# range of MAXMSG_ALERTSCRIPT values -- so the cut lands at every offset in the
# run -- the recorded message must never end in a lone lead byte (>= 0xC0): a
# whole character ends in an ASCII byte (< 0x80) or a continuation byte
# (0x80-0xBF). Two runs: "e'" (C3 A9, two bytes) and a flame emoji (F0 9F 94 A5,
# four bytes). The four-byte run is what pins the back-up floor: with the floor
# one short, a cut whose four-byte lead sits three bytes back leaves that lead
# alone at the end, which this catches.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
# shellcheck source=tests/lib/build-harness.sh
. "$(dirname "$0")/../lib/build-harness.sh"

ROOT=$(find_root)
here=$(dirname "$0")
require_c_buildenv "$ROOT"
[ -f "$(xymon_lib "$ROOT" libxymon.a)" ] \
	|| skip "tree not built (run make first; the post-build CI suite covers this)"

work=$(mktempdir)
build_xymon_libs "$ROOT" "$work/libbuild.log" libxymon.a libxymoncomm.a
# -iquote xymond: the harness #includes do_alert.c.
HARNESS_CFLAGS="-iquote $ROOT/xymond" build_harness "$work" "$work/harness" \
	"$here/alert-msg-utf8-harness.c" libxymon.a libxymoncomm.a

printf '1.2.3.4\ttesthost\t# conn\n' > "$work/hosts.cfg"
# The script records the raw bytes of $BBALPHAMSG, nothing else.
printf '#!/bin/sh\nprintf "%%s" "$BBALPHAMSG" > "%s/out"\n' "$work" > "$work/record.sh"
chmod +x "$work/record.sh"
printf 'HOST=testhost\n\tSCRIPT %s somebody FORMAT=SCRIPT\n' "$work/record.sh" > "$work/alerts.cfg"

# last_byte FILE -- the final byte of FILE as two lower-case hex digits.
last_byte() { od -An -tx1 "$1" | tr -d ' \n' | sed 's/.*\(..\)$/\1/'; }

# check_run NAME CHAR -- send "red conn " then 80 copies of CHAR, over a range
# of cut sizes, and fail if the cut ever leaves a lone lead byte at the end.
check_run() {
	local name=$1 char=$2 run msg n lb split_seen=no
	run=$char; local i; for i in $(seq 1 79); do run="$run$char"; done
	msg="red conn $run"
	# The run sits past a fixed-length header; 36..60 lands the cut inside it
	# at every offset, so an unfixed cut splits a character at some of them.
	for n in $(seq 36 60); do
		: > "$work/out"
		MAXMSG_ALERTSCRIPT=$n XYMONHOME="$work" XYMONTMP="$work" \
			"$work/harness" "$work/hosts.cfg" "$work/alerts.cfg" "$msg" >"$work/run.out" 2>"$work/run.err" || true
		[ -s "$work/out" ] || fail "$name: the alert script never ran at MAXMSG_ALERTSCRIPT=$n: $(cat "$work/run.err")"
		lb=$(last_byte "$work/out")
		case $lb in
			c[0-9a-f]|d[0-9a-f]|e[0-9a-f]|f[0-9a-f])
				fail "$name: at MAXMSG_ALERTSCRIPT=$n the message ended in a lone UTF-8 lead byte (0x$lb): the cut split a character" ;;
		esac
		[ "$(wc -c <"$work/out")" -lt "${#msg}" ] && split_seen=yes
	done
	[ "$split_seen" = yes ] || fail "$name: no MAXMSG_ALERTSCRIPT value truncated the message; the cut was never exercised"
}

check_run "two-byte" "$(printf '\303\251')"          # e-acute, C3 A9
check_run "four-byte" "$(printf '\360\237\224\245')" # flame, F0 9F 94 A5

pass "a SCRIPT alert's BBALPHAMSG cut does not split a two- or four-byte UTF-8 character"
