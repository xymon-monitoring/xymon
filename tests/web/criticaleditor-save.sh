#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/web/criticaleditor-save.sh
#
# The critical systems editor says what happened, and Next keeps its place.
# Runs the real criticaleditor.cgi against a scratch critical.cfg:
#
#   - Update with a host in hosts.cfg saves the record and says "Saved";
#   - Update with a host not in hosts.cfg, or without a test, writes nothing
#     and says why;
#   - a critical.cfg that cannot be written says "NOT SAVED" and why, where
#     it used to come back looking saved;
#   - Next from a host and test with no record goes to the next one after
#     them, where it used to blank the form; Next from the start of a host
#     name finds that host; Next from the last record stays on it, and says so;
#   - Drop with nothing filled in says so, where it used to crash; so does
#     Update without the monitoring-time fields the form always sends;
#   - what the user typed is escaped in the message.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
require_c_buildenv "$ROOT"

pcre_libs=${PCRELIBS:-$(xymon_makevar "$ROOT" PCRELIBS)}
if [ -z "$pcre_libs" ] && command -v pkg-config >/dev/null 2>&1; then
	pcre_libs=$(pkg-config --libs libpcre2-8 2>/dev/null || true)
fi
[ -n "$pcre_libs" ] || pcre_libs="-lpcre2-8"

work=$(mktempdir)
build_xymon_libs "$ROOT" "$work/libbuild.log" libxymon.a libxymoncomm.a

harness_cflags=$(xymon_cflags "$ROOT")
harness_ldflags=$(xymon_ldflags "$ROOT")
"$CC" $harness_cflags -o "$work/criticaleditor" "$ROOT/web/criticaleditor.c" \
	"$(xymon_lib "$ROOT" libxymon.a)" "$(xymon_lib "$ROOT" libxymoncomm.a)" "$(xymon_lib "$ROOT" libxymon.a)" \
	$pcre_libs $harness_ldflags 2>"$work/cc.log" \
	|| fail "criticaleditor does not compile: $(cat "$work/cc.log")"

mkdir -p "$work/web" "$work/etc"
cp "$ROOT"/xymond/webfiles/critedit_* "$work/web/"
printf '127.0.0.1 alpha.example.com # \n127.0.0.1 charlie.example.com # \n127.0.0.1 mail.example.com # \n' >"$work/etc/hosts.cfg"

edit() {	# QUERY_STRING [config file]
	OUT=$(REQUEST_METHOD=GET QUERY_STRING="$1" XYMONHOME="$work" \
		HOSTSCFG="!$work/etc/hosts.cfg" \
		"$work/criticaleditor" --config="${2:-$work/etc/critical.cfg}" 2>/dev/null) \
		|| fail "criticaleditor failed (rc $?) for '$1'"
	# the host pull-down's selected entry; the test is a text box here (no xymond)
	HOST=$(sed -n '/NAME="HOSTNAME"/,/<\/SELECT>/s/.*<option value="\([^"]*\)" selected>.*/\1/p' <<<"$OUT")
	SVC=$(sed -n 's/.*NAME="SERVICE"[^>]*VALUE="\([^"]*\)".*/\1/p' <<<"$OUT")
}

: >"$work/etc/critical.cfg"

# no CRITWKDAYS/CRITSTARTHOUR/CRITENDHOUR: these used to crash Update
edit "HOSTNAME=alpha.example.com&SERVICE=conn&PRIORITY=1&Update=1"
assert_contains "Saved" "$OUT" "a valid record did not say it was saved"
grep -q '^alpha.example.com|conn|' "$work/etc/critical.cfg" || fail "a valid record was not written: $(cat "$work/etc/critical.cfg")"

# as the browser sends it, with the monitoring time
edit "HOSTNAME=charlie.example.com&SERVICE=conn&PRIORITY=2&CRITWKDAYS=W&CRITSTARTHOUR=0700&CRITENDHOUR=2000&Update=1"
grep -q '^charlie.example.com|conn|.*W:0700:2000' "$work/etc/critical.cfg" || fail "the monitoring time was not saved: $(cat "$work/etc/critical.cfg")"
edit "HOSTNAME=mail.example.com&SERVICE=smtp&PRIORITY=2&Update=1"
before=$(cat "$work/etc/critical.cfg")

edit "HOSTNAME=nosuch.example.com&SERVICE=conn&PRIORITY=1&Update=1"
assert_contains "NOT SAVED: there is no host nosuch.example.com in hosts.cfg" "$OUT" "an unknown host was not refused"
[ "$(cat "$work/etc/critical.cfg")" = "$before" ] || fail "an unknown host changed critical.cfg"

edit "HOSTNAME=alpha.example.com&PRIORITY=1&Update=1"
assert_contains "NOT SAVED: enter both a host and a test" "$OUT" "a record without a test was not refused"

# a path that cannot be opened for writing, whoever runs the test
edit "HOSTNAME=alpha.example.com&SERVICE=conn&PRIORITY=1&Update=1" "$work/nodir/critical.cfg"
assert_contains "NOT SAVED: cannot write critical.cfg:" "$OUT" "a failed save did not say so"
assert_not_contains ">Saved<" "$OUT" "a failed save still said Saved"

# Next from a host and test with no record: the record after them
edit "HOSTNAME=bravo.example.com&SERVICE=conn&Next=1"
[ "$HOST|$SVC" = "charlie.example.com|conn" ] || fail "Next from a missing record went to '$HOST|$SVC', not charlie.example.com|conn"

# Next from the start of a host name: that host
edit "HOSTNAME=mail&Next=1"
[ "$HOST|$SVC" = "mail.example.com|smtp" ] || fail "Next from 'mail' went to '$HOST|$SVC', not mail.example.com|smtp"

# Next from the last record: stays, and says so
edit "HOSTNAME=mail.example.com&SERVICE=smtp&Next=1"
[ "$HOST|$SVC" = "mail.example.com|smtp" ] || fail "Next from the last record went to '$HOST|$SVC' instead of staying"
assert_contains "No configured critical system after this one" "$OUT" "Next from the last record did not say so"

edit "Drop=1"
assert_contains "Nothing deleted: enter both a host and a test" "$OUT" "Drop with nothing filled in did not say so"

edit "HOSTNAME=%3Cb%3Ex%3C%2Fb%3E&SERVICE=conn&PRIORITY=1&Update=1"
assert_contains "&lt;b&gt;x&lt;/b&gt;" "$OUT" "the typed host name is not escaped in the message"

edit "HOSTNAME=charlie.example.com&SERVICE=conn&Drop=1"
assert_contains "Deleted" "$OUT" "a delete did not say so"
grep -q '^charlie.example.com|' "$work/etc/critical.cfg" && fail "a deleted record is still in critical.cfg"

pass "the critical systems editor says whether a change was saved and why not, and Next keeps its place"
