#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/mailack-mime-body.sh
#
# xymon-mailack decodes the body of an alert reply for the ack cause: a
# single-part base64 or quoted-printable body, and the first text/plain part of
# a multipart message. --debug prints the xymondack message without sending.

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
run() { env XYMONHOME="$work" XYMONTMP="$work/tmp" \
	"$MAILACK" --debug --env="$work/xymonserver.cfg" 2>/dev/null | grep '^xymondack' || true; }

eacute=$(printf '\303\251')	# C3 A9

# 1. a single-part base64 body is decoded for the cause
out=$(printf 'From: a@b\nSubject: Re: Xymon [1234] RED\nContent-Transfer-Encoding: base64\n\nYWxsIGdvb2QsIG5vdyBmaXhlZA==\n' | run)
assert_contains "xymondack 1234 60 all good, now fixed" "$out" \
	"a base64 body was not decoded into the ack cause"

# 2. a quoted-printable body is decoded (incl a =XX byte and a soft line break)
out=$(printf 'From: a@b\nSubject: Re: Xymon [1234] RED\nContent-Transfer-Encoding: quoted-printable\n\ncaf=C3=A9 =\nfixed\n' | run)
assert_contains "xymondack 1234 60 caf${eacute} fixed" "$out" \
	"a quoted-printable body was not decoded (=XX or soft break)"

# 3. multipart/alternative: the text/plain part is used, the text/html ignored
out=$(printf 'From: a@b\nSubject: Re: Xymon [1234] RED\nContent-Type: multipart/alternative; boundary="BND"\n\n--BND\nContent-Type: text/html\n\n<p>ignore me</p>\n--BND\nContent-Type: text/plain\nContent-Transfer-Encoding: base64\n\ncGxhaW4gY2F1c2UgaGVyZQ==\n--BND--\n' | run)
assert_contains "xymondack 1234 60 plain cause here" "$out" \
	"the text/plain part of a multipart body was not chosen and decoded"

# 4. a multipart body with no text/plain part leaves the default cause, no crash
out=$(printf 'From: a@b\nSubject: Re: Xymon [1234] RED\nContent-Type: multipart/alternative; boundary="BND"\n\n--BND\nContent-Type: text/html\n\n<p>only html</p>\n--BND--\n' | run)
assert_contains "xymondack 1234 60 <No cause specified>" "$out" \
	"a multipart body with no text/plain part should fall back to the default cause"

# 5. nested multipart (a text/plain inside an inner multipart) is reached
out=$(printf 'From: a@b\nSubject: Re: Xymon [1234] RED\nContent-Type: multipart/mixed; boundary="OUT"\n\n--OUT\nContent-Type: multipart/alternative; boundary="IN"\n\n--IN\nContent-Type: text/plain\n\nnested cause\n--IN--\n--OUT--\n' | run)
assert_contains "xymondack 1234 60 nested cause" "$out" \
	"a text/plain nested one multipart deep was not reached"

# 5b. a nested part whose inner Content-Type folds "boundary=" onto a
# continuation line (as Gmail writes it) is still reached: the part parser
# must unfold the header, and the body must keep the leading whitespace that
# marks the fold
out=$(printf 'From: a@b\nSubject: Re: Xymon [1234] RED\nContent-Type: multipart/mixed; boundary="OUT"\n\n--OUT\nContent-Type: multipart/alternative;\n\tboundary="IN"\n\n--IN\nContent-Type: text/plain\n\nlooking at it now\n--IN--\n--OUT--\n' | run)
assert_contains "xymondack 1234 60 looking at it now" "$out" \
	"a text/plain part was missed when the inner boundary= was folded onto a continuation line"

# 6. a missing closing boundary: the part still runs to end of message
out=$(printf 'From: a@b\nSubject: Re: Xymon [1234] RED\nContent-Type: multipart/alternative; boundary="BND"\n\n--BND\nContent-Type: text/plain\n\nunterminated cause\n' | run)
assert_contains "xymondack 1234 60 unterminated cause" "$out" \
	"a part with no closing boundary was not read to the end"

# 7. a content line that merely starts with the boundary string is not a
# delimiter (a delimiter line is the boundary and nothing but trailing space
# or the closing "--"), so it is kept as part content
out=$(printf 'From: a@b\nSubject: Re: Xymon [1234] RED\nContent-Type: multipart/alternative; boundary="BND"\n\n--BND\nContent-Type: text/plain\n\n--BNDoodle is my note\n' | run)
assert_contains "xymondack 1234 60 --BNDoodle is my note" "$out" \
	"a content line starting with the boundary prefix was wrongly treated as a delimiter"

pass "xymon-mailack decodes base64/quoted-printable bodies and the first text/plain part of a multipart"
