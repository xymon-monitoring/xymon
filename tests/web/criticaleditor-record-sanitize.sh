#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/web/criticaleditor-record-sanitize.sh
#
# A record in critical.cfg is one line, its fields split on "|". The Critical
# Systems editor writes the resolver group, instructions and clone names from
# what the form sent, so a "|" or a newline in one of them would add a field
# or a whole record to the file. The editor now replaces "|", CR and LF in
# those values with a space, so a value stays inside its field and its line.
#
# Checked with the real criticaleditor.cgi: an Update whose GROUP holds a "|"
# and a newline, and whose EXTRA holds a "|", leaves critical.cfg with one
# record for the edited host and none for the host named in the payload.

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
# shellcheck disable=SC2086
"$CC" $harness_cflags -o "$work/criticaleditor" "$ROOT/web/criticaleditor.c" \
	"$(xymon_lib "$ROOT" libxymon.a)" "$(xymon_lib "$ROOT" libxymoncomm.a)" "$(xymon_lib "$ROOT" libxymon.a)" \
	$pcre_libs $harness_ldflags 2>"$work/cc.log" \
	|| fail "criticaleditor does not compile: $(cat "$work/cc.log")"

mkdir -p "$work/web" "$work/etc"
cp "$ROOT"/xymond/webfiles/critedit_* "$work/web/"
printf '127.0.0.1 host.example # \n' >"$work/etc/hosts.cfg"
printf 'host.example|conn||||1|team||x\n' >"$work/etc/critical.cfg"

# GROUP = x, a newline, then a full extra record for other.example; EXTRA = a|b.
# The "|" is URL %7C, the newline %0A.
group='x%0Aother.example%7Cconn%7C%7C%7C%7C1%7Cevil%7C%7C'
body="Update=Update&HOSTNAME=host.example&SERVICE=conn&PRIORITY=1&GROUP=${group}&EXTRA=a%7Cb&CRITWKDAYS=*&CRITSTARTHOUR=0000&CRITENDHOUR=2400"

printf '%s' "$body" | env REQUEST_METHOD=POST \
	CONTENT_TYPE=application/x-www-form-urlencoded CONTENT_LENGTH=${#body} \
	HTTP_HOST=xymon.example SECURECGIBINURL=/xymon-seccgi \
	HTTP_REFERER=http://xymon.example/xymon-seccgi/criticaleditor.sh \
	REMOTE_USER=alice XYMONHOME="$work" HOSTSCFG="!$work/etc/hosts.cfg" \
	"$work/criticaleditor" --config="$work/etc/critical.cfg" >/dev/null 2>"$work/err" \
	|| fail "criticaleditor failed (rc $?): $(cat "$work/err")"

# No record was injected for the host named inside the payload: the "|" and
# newline in the group are now spaces, so "other.example" is text in the group
# field of the one record, not a record of its own.
injected=$(grep -c '^other\.example|' "$work/etc/critical.cfg" || true)
assert_equal "0" "$injected" "a newline in the group added a record for another host"
# The edited host keeps exactly one record (its key line).
hostlines=$(grep -c '^host\.example|conn|' "$work/etc/critical.cfg" || true)
assert_equal "1" "$hostlines" "the edited host does not have exactly one record"
# The whole file is that one record, with no stray line from the split.
total=$(grep -c . "$work/etc/critical.cfg" || true)
assert_equal "1" "$total" "critical.cfg has more than the one record it should"

pass "the editor keeps a pipe or newline in a value inside its field, with no record injected"
