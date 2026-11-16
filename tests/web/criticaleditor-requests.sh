#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/web/criticaleditor-requests.sh
#
# The critical systems editor answers a request with a field missing instead
# of crashing. Runs the real criticaleditor.cgi against a scratch critical.cfg:
#
#   - Drop without a host, without a test, or without both, says nothing was
#     deleted (strlen(NULL) in deleterecord() before);
#   - Update without the monitoring-time fields saves the record with the
#     form's defaults (strlen(NULL) in parse_query() before). A browser
#     always sends them, a shorter request need not. Each missing field takes
#     its own default: every day, from 0000, to 2400. All three together are
#     the 24x7 record, which is stored as an empty time field;
#   - Clone with hosts to add but no host to clone from says nothing was
#     cloned (a NULL origin in addclone_critconfig() before);
#   - Clone with a host but no hosts to add says nothing was cloned
#     (strtok(NULL) resumed cgi_request()'s freed buffer before).

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
printf '127.0.0.1 alpha.example.com # \n' >"$work/etc/hosts.cfg"
: >"$work/etc/critical.cfg"

edit() {	# QUERY_STRING
	rc=0
	OUT=$(REQUEST_METHOD=GET QUERY_STRING="$1" XYMONHOME="$work" \
		HOSTSCFG="!$work/etc/hosts.cfg" \
		"$work/criticaleditor" --config="$work/etc/critical.cfg" 2>/dev/null) || rc=$?
	[ "$rc" -eq 0 ] || fail "criticaleditor failed (rc $rc) for '$1'"
}

for q in "Drop=1" "HOSTNAME=alpha.example.com&Drop=1" "SERVICE=conn&Drop=1"; do
	edit "$q"
	assert_contains "Nothing deleted: enter both a host and a test" "$OUT" "Drop with a field missing ('$q') did not say so"
done

# The stored time field (the fifth) for a test, as written by an Update.
crittime() {
	awk -F'|' -v svc="$1" '$1 == "alpha.example.com" && $2 == svc { print $5 }' "$work/etc/critical.cfg"
}
saved() {	# test, expected time field, what was left out
	grep -q "^alpha.example.com|$1|" "$work/etc/critical.cfg" \
		|| fail "Update $3 did not save: $(cat "$work/etc/critical.cfg")"
	[ "$(crittime "$1")" = "$2" ] \
		|| fail "Update $3 stored the time '$(crittime "$1")', not '$2'"
}

edit "HOSTNAME=alpha.example.com&SERVICE=conn&PRIORITY=1&Update=1"
saved conn "" "without the time fields"
edit "HOSTNAME=alpha.example.com&SERVICE=procs&PRIORITY=1&CRITWKDAYS=*&CRITSTARTHOUR=0000&CRITENDHOUR=2400&Update=1"
saved procs "" "with the form's 24x7 values"

edit "HOSTNAME=alpha.example.com&SERVICE=cpu&PRIORITY=1&CRITWKDAYS=W&Update=1"
saved cpu "W:0000:2400" "with only the days"
edit "HOSTNAME=alpha.example.com&SERVICE=disk&PRIORITY=1&CRITSTARTHOUR=0800&Update=1"
saved disk "*:0800:2400" "with only the start hour"
edit "HOSTNAME=alpha.example.com&SERVICE=memory&PRIORITY=1&CRITENDHOUR=1700&Update=1"
saved memory "*:0000:1700" "with only the end hour"

before=$(cat "$work/etc/critical.cfg")
edit "CRITEDITADDCLONES=beta.example.com&Clone=1"
assert_contains "Nothing cloned: enter the host to clone from" "$OUT" "Clone without a host did not say so"
[ "$(cat "$work/etc/critical.cfg")" = "$before" ] || fail "Clone without a host changed critical.cfg"

edit "HOSTNAME=alpha.example.com&Clone=1"
assert_contains "Nothing cloned: enter the hosts to clone to" "$OUT" "Clone with no hosts to add did not say so"
[ "$(cat "$work/etc/critical.cfg")" = "$before" ] || fail "Clone with no hosts to add changed critical.cfg"

pass "the critical systems editor answers a Drop, Update or Clone with a field missing instead of crashing"
