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
for svc in smtp smtps submission msa; do
	block=$(service_block "$svc")
	[ -n "$block" ] || fail "protocols.cfg no longer defines [$svc] (#450)"

	if grep -Eq '^[[:space:]]*send[[:space:]]' <<<"$block"; then
		fail "[$svc] sends before the server's greeting -- xymonnet writes before it reads, so this is a pregreet, and Postfix >= 3.9 answers 554 (#450)"
	fi

	# The probe still has to check something. Without expect, a service that
	# accepts the connection and says nothing useful would pass, which would
	# make removing the send a downgrade rather than a fix.
	grep -Eq '^[[:space:]]*expect[[:space:]]+"220"' <<<"$block" || fail \
		"[$svc] no longer expects a 220 greeting -- the probe would accept any banner (#450)"
	# Compare option TOKENS, not substrings: "options nobanner" or
	# "bannerless" would satisfy a substring match while meaning the opposite.
	opts=$(sed -n 's/^[[:space:]]*options[[:space:]]*//p' <<<"$block" | tr ',' ' ')
	has_opt() { printf '%s\n' $opts | grep -qx "$1"; }

	has_opt banner || fail \
		"[$svc] no longer asks for the banner -- with no send, the greeting is the only thing this probe can check (#450)"

	# smtps is the TLS variant; losing the ssl option would silently turn it
	# into a plaintext probe of a TLS port that still passes its 220 check.
	if [ "$svc" = "smtps" ]; then
		has_opt ssl || fail "[smtps] no longer requests an SSL handshake (#450)"
	fi
done

# protocols.cfg is not the only place these probes are defined. lib/netservices.c
# carries a compiled-in default_svcinfo[] table, used whenever protocols.cfg
# cannot be opened (init_tcp_services() falls back to it and says so). Its smtp
# and smtps rows had the same shape of defect -- "mail\r\nquit\r\n", a pregreet
# and two commands in one write -- so fixing only the config would leave a
# violating probe one unreadable file away.
NETSVC="$ROOT/lib/netservices.c"
if [ -f "$NETSVC" ]; then
	for svc in smtp smtps; do
		row=$(grep -E "^[[:space:]]*\{[[:space:]]*\"$svc\"," "$NETSVC")
		[ -n "$row" ] || fail "lib/netservices.c no longer defines a fallback for $svc (#450)"
		# Field 2 of the row is the data to send. Assert it is NULL rather than
		# merely absent of a known string: any replacement literal would be a
		# pregreet just the same.
		grep -Eq "^[[:space:]]*\{[[:space:]]*\"$svc\",[[:space:]]*NULL," <<<"$row" || fail \
			"lib/netservices.c fallback for $svc still sends before the greeting (#450): $row"
		# The rest of the row still has to describe the same probe: without the
		# 220 expectation or the banner flag, removing the send would quietly
		# downgrade the fallback from "checks the greeting" to "connects".
		grep -q '"220"' <<<"$row" || fail \
			"lib/netservices.c fallback for $svc no longer expects a 220 greeting (#450): $row"
		grep -q 'TCP_GET_BANNER' <<<"$row" || fail \
			"lib/netservices.c fallback for $svc no longer grabs the banner (#450): $row"
		if [ "$svc" = "smtps" ]; then
			grep -q 'TCP_SSL' <<<"$row" || fail \
				"lib/netservices.c fallback for smtps lost TCP_SSL (#450): $row"
		fi
	done
fi

pass "the SMTP probes wait for the greeting instead of pipelining into it (#450)"
