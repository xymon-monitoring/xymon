#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/web/acklog-escape.sh
#
# Two renderers read acknowledge.log: do_acklog() (lib/acklog.c), the table
# at the bottom of the non-green page, and do_acknowledgementslog()
# (lib/acknowledgementslog.c), acknowledgements.cgi's report. An
# acknowledgement's message and "Acked by" name are whatever the acknowledger
# typed, so both have to be escaped before they reach a page. With the real
# functions over one acknowledge.log, checked in each:
#
#   - a message holding markup, %-encoded as the acknowledge page sends it,
#     comes out as text in the ack table, where it is %-decoded;
#   - a message holding markup as it is, as xymond logs an acknowledgement
#     sent to it, comes out as text in both;
#   - the same for an "Acked by:" name carried in the message;
#   - a plain message still renders as written.

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
mkdir -p "$work/logs" "$work/acks"

build_xymon_libs "$ROOT" "$work/libbuild.log" libxymoncomm.a libxymon.a

printf '1.2.3.4\tackhost\t# cpu\n1.2.3.5\tack<u>host\t# \n' > "$work/hosts.cfg"

now=$(date +%s)
# acknowledge.log records: a huge duration so none reads as expired, acktime
# "now" so each falls inside do_acklog's 240-minute window, host "ackhost" so
# hostinfo() resolves it. The message is the rest of the line after the
# seventh TAB; "np_filename_not_used" makes the "Acked by:" in it the name.
# The fifth has markup in the host and test names instead.
{
	printf '%s\t1\t100000\t1\tnp_filename_not_used\tackhost.cpu\tred\t%s\n' \
		"$now" "%3Cb%3Ebold%3C/b%3E"
	printf '%s\t2\t100000\t2\tnp_filename_not_used\tackhost.cpu\tred\t%s\n' \
		"$now" "on it%0AAcked by:%3Ci%3Ename%3C/i%3E"
	printf '%s\t3\t100000\t3\tnp_filename_not_used\tackhost.cpu\tred\t%s\n' \
		"$now" "disk replaced, watching"
	printf '%s\t4\t100000\t4\tnp_filename_not_used\tackhost.cpu\tred\t%s\n' \
		"$now" "<b>raw</b>\\nAcked by:<i>who</i>"
	printf '%s\t5\t100000\t5\tnp_filename_not_used\tack<u>host.c<s>pu\tred\t%s\n' \
		"$now" "named host"
} > "$work/logs/acknowledge.log"

build_harness "$work" "$work/harness" \
	"$here/acklog-escape-harness.c" libxymon.a libxymoncomm.a libxymon.a

# The "!" prefix forces load_hostnames() to read the file instead of asking a
# xymond that is not running (lib/loadhosts_file.c).
XYMONSERVERLOGS="$work/logs" HOSTSCFG="!$work/hosts.cfg" XYMONACKDIR="$work/acks" \
	"$work/harness" "!$work/hosts.cfg" > "$work/out" 2>"$work/err" \
	|| fail "the harness failed: $(cat "$work/err")"
table=$(sed -n '1,/^=====$/p' "$work/out")
report=$(sed -n '/^=====$/,$p' "$work/out")

for part in table report; do
	out=${!part}
	assert_contains "disk replaced, watching" "$out" \
		"$part: a plain acknowledgement message did not render, so it was not written"
	for tag in "<b>" "<i>" "<u>" "<s>"; do
		assert_not_contains "$tag" "$out" "$part: an acknowledgement's text reached the page as markup ($tag)"
	done
	assert_contains "ack&lt;u&gt;host" "$out" "$part: a host name was not shown escaped"
	assert_contains "c&lt;s&gt;pu" "$out" "$part: a test name was not shown escaped"
	# (the report trims a character on each side of "\nAcked by:", as it always has)
	assert_contains "&lt;b&gt;raw&lt;/b" "$out" "$part: a message as xymond logs it was not shown escaped"
done
assert_contains "&lt;b&gt;bold&lt;/b&gt;" "$table" "table: a %-encoded message was not shown escaped"
assert_contains "&lt;i&gt;name&lt;/i&gt;" "$table" "table: an \"Acked by\" name was not shown escaped"
assert_contains "who&lt;/i&gt;" "$report" "report: an \"Acked by\" name was not shown escaped"

pass "the ack table and acknowledgements.cgi's report escape an acknowledgement's message and \"Acked by\" name"
