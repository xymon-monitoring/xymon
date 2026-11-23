#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/mailack-rfc2047.sh
#
# xymon-mailack decodes RFC 2047 encoded-words in the From and Subject headers
# of an alert reply before it parses them. --debug prints the xymondack message
# and returns without sending, so no daemon is needed (as mailack-cookie.sh).
# Checks: the From name reaches "Acked by:" decoded; a fully-encoded Subject
# still yields an ack (decode runs before the cookie regex, which needs a real
# space -- the bug this fixes); an encoded MSG= cause is decoded; a folded
# header is unfolded; and a decoded newline cannot inject a second line.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin MAILACK "xymond/xymon-mailack"
export LC_ALL=C

work=$(mktempdir)
mkdir -p "$work/tmp"
cat > "$work/xymonserver.cfg" <<CFG
XYMONHOME="$work"
XYMONTMP="$work/tmp"
XYMSRV="127.0.0.1"
XYMONDPORT="1984"
CFG

# run MAIL... -- feed the mail on stdin, print what it would send
run() { env XYMONHOME="$work" XYMONTMP="$work/tmp" \
	"$MAILACK" --debug --env="$work/xymonserver.cfg" 2>/dev/null; }

o_umlaut=$(printf '\303\266')	# o-umlaut UTF-8, C3 B6
eacute=$(printf '\303\251')	# e-acute UTF-8, C3 A9

# 1. From encoded-word decoded onto the Acked by line.
out=$(printf 'From: =?UTF-8?Q?Bj=C3=B6rn?= <b@x>\nSubject: Re: Xymon [1234] db1 RED\n\nok\n' | run)
assert_contains "Acked by: Bj${o_umlaut}rn" "$out" \
	"an encoded From display name was not decoded on the Acked by line"

# 2. A fully-encoded Subject still produces an ack (decode before the cookie regex).
out=$(printf 'From: a@b\nSubject: =?UTF-8?Q?Re:_Xymon_[1234]_db1_RED?=\n\nok\n' | run | grep '^xymondack' || true)
assert_contains "xymondack 1234" "$out" \
	"a fully-encoded Subject produced no ack -- the Subject must be decoded before the cookie regex"

# 3. An encoded MSG= cause is decoded.
out=$(printf 'From: a@b\nSubject: Re: Xymon [1234] db1 MSG= =?UTF-8?B?w6k=?=\n\n' | run | grep '^xymondack' || true)
assert_contains "xymondack 1234 60 ${eacute}" "$out" \
	"an encoded MSG= cause was not decoded"

# 4. A folded From header is unfolded, then decoded.
out=$(printf 'From: =?UTF-8?Q?Bj?=\n =?UTF-8?Q?=C3=B6rn?= <b@x>\nSubject: Re: Xymon [1234] RED\n\nok\n' | run)
assert_contains "Acked by: Bj${o_umlaut}rn" "$out" \
	"a folded, encoded From header was not unfolded and decoded"

# 5. A decoded newline must not inject a second line.
out=$(printf 'From: =?UTF-8?Q?x=0Axymondack_9_1_evil?= <b@x>\nSubject: Re: Xymon [1234] RED\n\nok\n' | run)
n=$(printf '%s\n' "$out" | grep -c '^xymondack' || true)
assert_equal "1" "$n" "a decoded newline in From injected an extra xymondack line"

pass "xymon-mailack decodes RFC 2047 From/Subject before parsing, unfolds, and scrubs injected newlines"
