#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/web/statusreport-filters.sh
#
# statusreport.cgi builds its xymondboard filter from --column, --test and
# --filter, growing it with realloc(). Since the 2019 buffer fixes the filter
# started as the literal "", so the first of those options handed realloc() a
# string literal and the CGI aborted ("realloc(): invalid pointer").
# certreport.cgi, which cgiwrap builds on it, passes such options.
#
# Against a real xymond, with hosta (red sslcert) and hostc (green sslcert)
# on page one and hostb (red conn) on page two:
#   - run as certreport.cgi runs it, it is served and lists hosta alone;
#   - --test=sslcert works as --column=sslcert;
#   - a filter given from a page, without --all, keeps to that page;
#   - with no filter it keeps to the pagepath cookie's page, as the man page
#     says and as it did, and with --all lists every host.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
# shellcheck source=tests/lib/xymond-daemon.sh
. "$(dirname "$0")/../lib/xymond-daemon.sh"

require_bin STATUSREPORT web/statusreport.cgi
require_bin XYMOND xymond/xymond
require_bin XYMONCLIENT common/xymon
require_shm_segments "$(grep -c 'setup_channel(C_[A-Z_]*, CHAN_MASTER)' "$(find_root)/xymond/xymond.c")"
require_cfg XYMONSERVER_CFG xymond/etcfiles/xymonserver.cfg

work=$(mktempdir)
mkdir -p "$work/home/tmp" "$work/home/web"
{
	printf 'page one One\n127.0.0.1 hosta.example.com # \n127.0.0.1 hostc.example.com # \n'
	printf 'page two Two\n127.0.0.1 hostb.example.com # \n'
} >"$work/hosts.cfg"
sed -e 's|^XYMONHOME=.*|XYMONHOME="'"$work"'/home"|' \
    -e 's|^XYMONTMP=.*|XYMONTMP="'"$work"'/home/tmp"|' \
	"$XYMONSERVER_CFG" >"$work/xymonserver.cfg"

XYMOND_PID=""
stop_xymond() {
	[ -n "$XYMOND_PID" ] || return 0
	stop_process "$XYMOND_PID"
	XYMOND_PID=""
}
register_cleanup stop_xymond
xymond_launch() {
	local port=$1; shift
	"$XYMOND" --no-daemon --listen="127.0.0.1:$port" \
		--hosts="$work/hosts.cfg" --env="$work/xymonserver.cfg" \
		--pidfile="$work/xymond.pid" --checkpoint-file="$work/chk.out" \
		"$@" >"$work/xymond.log" 2>&1 &
	XYMOND_PID=$!
}
start_xymond
printf 'XYMSRV="127.0.0.1"\nXYMONDPORT="%s"\n' "$PORT" >>"$work/xymonserver.cfg"

x() { XYMON_TIMEOUT=5 "$XYMONCLIENT" "127.0.0.1:$PORT" "$1"; }
x "status hosta,example,com.sslcert red certificate expired" >/dev/null
x "status hostb,example,com.conn red down" >/dev/null
x "status hostc,example,com.sslcert green certificate valid" >/dev/null
ready=
for _ in {1..50}; do
	[ "$(x 'xymondboard test=^(sslcert|conn)$ fields=hostname' | wc -l)" -ge 3 ] && { ready=1; break; }
	sleep 0.1
done
[ -n "$ready" ] || fail "xymond did not take the three statuses: $(x 'xymondboard fields=hostname,testname,color' 2>&1)"

# report COOKIE ARGS... -- statusreport.cgi's page, as a GET; its exit in RC
report() {
	local cookie=$1
	shift
	set +e
	OUT=$(env REQUEST_METHOD=GET QUERY_STRING="" HTTP_COOKIE="$cookie" XYMONHOME="$work/home" \
		"$STATUSREPORT" --env="$work/xymonserver.cfg" "$@" 2>&1)
	RC=$?
	set -e
}

# As certreport.cgi runs it
report "" --column=sslcert --filter=color=red,yellow --all --no-colors
[ "$RC" -eq 0 ] || fail "statusreport.cgi, run as certreport.cgi, was not served (exit $RC): $OUT"
assert_contains "hosta.example.com" "$OUT" "the certificate report does not list the host with a red sslcert"
assert_not_contains "hostb.example.com" "$OUT" "the certificate report lists a host whose red status is not sslcert"
assert_not_contains "hostc.example.com" "$OUT" "the certificate report lists a host whose sslcert is green"

report "" --test=sslcert --filter=color=red --all
[ "$RC" -eq 0 ] || fail "statusreport.cgi --test=sslcert was not served (exit $RC): $OUT"
assert_contains "hosta.example.com" "$OUT" "--test=sslcert does not list the host with a red sslcert"
assert_not_contains "hostb.example.com" "$OUT" "--test=sslcert lists a host whose red status is not sslcert"

# A filter from a page, without --all: the filter, kept to that page
report "pagepath=one" --column=sslcert --filter=color=red
[ "$RC" -eq 0 ] || fail "statusreport.cgi --column from a page was not served (exit $RC): $OUT"
assert_contains "hosta.example.com" "$OUT" "a filter from page one does not list that page's red sslcert"
report "pagepath=two" --column=sslcert --filter=color=red
[ "$RC" -eq 0 ] || fail "statusreport.cgi --column from page two was not served (exit $RC): $OUT"
assert_not_contains "hosta.example.com" "$OUT" "a filter from page two lists page one's host: the page was not kept to"

# No filter: the page the viewer came from, or with --all every host
report "pagepath=one" --heading=all
[ "$RC" -eq 0 ] || fail "statusreport.cgi without a filter was not served (exit $RC): $OUT"
assert_contains "hosta.example.com" "$OUT" "with no filter, the report left out the cookie's page"
assert_not_contains "hostb.example.com" "$OUT" "with no filter, the report did not keep to the cookie's page"
report "pagepath=one" --heading=all --all
[ "$RC" -eq 0 ] || fail "statusreport.cgi --all was not served (exit $RC): $OUT"
assert_contains "hostb.example.com" "$OUT" "with --all, the report kept to the cookie's page"

pass "statusreport.cgi takes --column, --test and --filter without aborting, and with no filter keeps to the viewer's page unless --all"
