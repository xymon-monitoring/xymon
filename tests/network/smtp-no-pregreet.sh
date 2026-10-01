#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/network/smtp-no-pregreet.sh
#
# Guard for xymon-monitoring/xymon#450: the SMTP probes must not speak before
# the server greets them.
#
# do_tcp_tests() writes before it reads -- socket_write() runs in the writable
# branch, the banner is read on a later pass -- so any `send` in protocols.cfg
# goes out ahead of the server's 220. The SMTP entries used to send
# "ehlo xymonnet\r\nquit\r\n": ahead of the greeting, and two commands in one
# write before PIPELINING was announced. Both violate RFC 5321/2920, and
# Postfix >= 3.9 enables smtpd_forbid_unauth_pipelining by default, answering
# with "554 5.5.0 Error: SMTP protocol synchronization".
#
# Nothing was lost by removing it. xymonnet reads once and compares `expect`
# against the START of the banner (tcp_got_expected), so the EHLO's own reply
# was never examined -- the send cost a protocol violation and bought nothing.
#
# This is a config assertion, not a static guard: protocols.cfg is the shipped
# artefact, and the file itself is the thing under test. Skips only if it is
# absent.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
CFG="$ROOT/xymonnet/protocols.cfg"
[ -f "$CFG" ] || skip "protocols.cfg absent"

# Extract one service's block: from its [header] to the next [header]. Blocks
# are not blank-line delimited -- [smtps] is followed by a comment before the
# next entry -- so terminating on a blank line would truncate and let a `send`
# hide in the tail. Header names alternate ([submission|msa]), so match the
# name as a whole alternative rather than a substring: a plain grep for "smtp"
# would also select [smtps], and one for "[smtp]" would miss [submission|msa].
service_block() {
	awk -v want="$1" '
		/^\[/ {
			inblock = 0
			hdr = $0
			sub(/^\[/, "", hdr); sub(/\].*$/, "", hdr)
			n = split(hdr, alts, "|")
			for (i = 1; i <= n; i++) if (alts[i] == want) inblock = 1
			if (inblock) next
		}
		inblock { print }
	' "$CFG"
}

# Every SMTP-speaking probe Xymon ships. submission/msa is the same protocol on
# 587 and was carrying the identical string, so a fix that covered only smtp
# and smtps would leave the same violation on another port. msa is listed
# separately from submission even though they share one [submission|msa] block
# today: if that block is ever split, the alias must not be able to reacquire a
# send while this test keeps passing.
for svc in smtp smtps submission msa submissiontls; do
	block=$(service_block "$svc")
	[ -n "$block" ] || fail "protocols.cfg no longer defines [$svc] (#450)"

	# The invariant is ordering, not abstinence: a send is fine, a send BEFORE
	# the first expect is the pregreet. Compare line positions rather than
	# looking for the absence of a send, so a legal dialogue passes and the old
	# shape still fails.
	# An entry with nothing to send cannot pregreet, and grep exiting 1 on it
	# must not end the test: every test runs under `set -o pipefail`, so an
	# unguarded no-match here would abort before a single assertion ran.
	first_send=$({ grep -nE '^[[:space:]]*send[[:space:]]' <<<"$block" || :; } | head -1 | cut -d: -f1)
	first_expect=$(grep -nE '^[[:space:]]*expect[[:space:]]' <<<"$block" | head -1 | cut -d: -f1)
	[ -n "$first_expect" ] || fail "[$svc] has no expect step at all (#450)"
	if [ -n "$first_send" ] && [ "$first_send" -lt "$first_expect" ]; then
		fail "[$svc] sends before its first expect -- that is a pregreet, and Postfix >= 3.9 answers 554 (#450)"
	fi

	# One command per send. "ehlo x\r\nquit\r\n" in a single write is
	# unauthorized pipelining even after the greeting -- measured: Postfix
	# rejects it 5/5 with "improper command pipelining after EHLO".
	while IFS= read -r sline; do
		[ -n "$sline" ] || continue
		body=${sline#*send }
		crlfs=$(grep -o '\\r\\n' <<<"$body" | wc -l | tr -d ' ')
		[ "$crlfs" -le 1 ] || fail \
			"[$svc] packs $crlfs commands into one send -- pipelining before PIPELINING is announced (#450): $sline"
	done <<<"$(grep -E '^[[:space:]]*send[[:space:]]' <<<"$block")"

	# The probe still has to check something. Without expect, a service that
	# accepts the connection and says nothing useful would pass, which would
	# make removing the send a downgrade rather than a fix.
	grep -Eq '^[[:space:]]*expect[[:space:]]+"220"' <<<"$block" || fail \
		"[$svc] no longer expects a 220 greeting -- the probe would accept any banner (#450)"
	# Compare option TOKENS, not substrings: "options nobanner" or
	# "bannerless" would satisfy a substring match while meaning the opposite.
	opts=$(sed -n 's/^[[:space:]]*options[[:space:]]*//p' <<<"$block" | tr ',' ' ')
	has_opt() { printf '%s\n' $opts | grep -qx "$1"; }

	# "options banner" is deliberately NOT required here. It decides one thing
	# -- whether the socket is read when no step asks to -- and the expect
	# above already asks. What the service said reaches the status either way,
	# so requiring the option would pin a line that changes nothing.

	# smtps is the TLS variant; losing the handshake would silently turn it
	# into a plaintext probe of a TLS port that still passes its 220 check.
	# Either spelling counts: "options ssl" is TLS from the first byte, and a
	# "start tls" ahead of every other step is the same thing said in order.
	if [ "$svc" = "smtps" ]; then
		if ! has_opt ssl && ! grep -qE '^[[:space:]]*start[[:space:]]+tls' <<<"$block"; then
			fail "[smtps] no longer requests an SSL handshake (#450)"
		fi
	fi
done

# protocols.cfg is not the only place these probes were defined. lib/netservices.c
# carries default_svcinfo[], and its smtp row used to hold "mail\r\nquit\r\n" --
# a pregreet, and two commands in one write -- used whenever protocols.cfg could
# not be opened. Fixing only the config left a violating probe one unreadable
# file away.
#
# That table is now empty -- a terminator and nothing else. It held names and
# default ports so that a missing protocols.cfg reported yellow instead of red,
# but it listed 28 of the 49 services the file defines, so the same fault came
# out yellow or red depending on which names were hardcoded. find_tcp_service()
# refuses every unknown service when the file cannot be read, which covers all
# of them (protocols-unreadable.sh holds that end).
#
# So the rule here is stronger than "smtp must not send": NO row may come back
# at all. A row can only restate a port protocols.cfg already gives, and drift
# from it.
NETSVC="$ROOT/lib/netservices.c"
if [ -f "$NETSVC" ]; then
	table=$(awk '/^static svcinfo_t default_svcinfo/{c=1} c{print} c&&/^};/{exit}' "$NETSVC")
	[ -n "$table" ] || fail "lib/netservices.c no longer defines default_svcinfo[] (#450)"

	# Any row with a name in it is a service definition that cannot run.
	offenders=$(grep -E '^[[:space:]]*\{[[:space:]]*"' <<<"$table" || true)
	[ -z "$offenders" ] || fail \
"lib/netservices.c has put service definitions back into default_svcinfo[]. They
cannot run -- an unreadable protocols.cfg refuses every service -- so they can
only restate what protocols.cfg says, and go stale against it (#450):
$offenders"
fi

pass "the SMTP probes wait for the greeting instead of pipelining into it (#450)"
