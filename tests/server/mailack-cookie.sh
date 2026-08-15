#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/mailack-cookie.sh
#
# xymon-mailack turns a reply to an alert mail into a xymondack message, using
# the cookie in the subject line. PCRE1's pcre_copy_substring() returned the
# substring's *length*, so "<= 0" meant failure; PCRE2's
# pcre2_substring_copy_bynumber() returns 0 for success and a negative error
# code otherwise, so the same test is true exactly when the copy worked.
#
# The PCRE2 migration converted two of the three extractions in that function
# and carried the third across unchanged, which left every acknowledgement
# bailing out with "Could not find cookie value" -- the feature had been
# entirely non-functional since.
#
# --debug prints the message it would send and returns without sending, so the
# whole path is testable without a xymond: the cookie, the default duration and
# the reply text all come from the parse this guards.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin MAILACK "xymond/xymon-mailack"

work=$(mktempdir)
mkdir -p "$work/etc" "$work/tmp"
cat > "$work/etc/xymonserver.cfg" <<EOF
XYMONHOME="$work"
XYMONTMP="$work/tmp"
XYMSRV="127.0.0.1"
XYMONDPORT="1984"
EOF

# mailack <subject-line> -- feed a reply and print what it would send
mailack() {
	{
		printf 'Return-Path: <ops@example.com>\n'
		printf 'From: Ops <ops@example.com>\n'
		[ -n "$1" ] && printf 'Subject: %s\n' "$1"
		printf '\n'
		printf 'acked, on it\n'
	} | env XYMONHOME="$work" XYMONTMP="$work/tmp" \
		"$MAILACK" --debug --env="$work/etc/xymonserver.cfg" 2>/dev/null \
		| grep '^xymondack' || true
}

# ---- the cookie reaches the ack ---------------------------------------------

got=$(mailack 'Re: Xymon [1234] db1:disk RED')
assert_contains "xymondack 1234" "$got" \
	"a reply carrying a cookie produced no acknowledgement -- the substring copy was read as failed"

# The rest of the line comes from the same parse, so assert it too: a build
# that recovered the cookie but lost the duration or the text would still be
# broken for the operator who sent the reply.
assert_contains "xymondack 1234 60 acked, on it" "$got" \
	"the acknowledgement did not carry the default duration and the reply text"

# ---- and nothing else does --------------------------------------------------
#
# Without these, the assertion above is satisfied by a build that acknowledges
# whatever it is given.

got=$(mailack 'Re: hello there')
assert_equal "" "$got" "a subject with no cookie must not produce an acknowledgement"

got=$(mailack '')
assert_equal "" "$got" "a mail with no subject at all must not produce an acknowledgement"

pass "a mailed reply carrying an alert cookie produces the matching xymondack"
