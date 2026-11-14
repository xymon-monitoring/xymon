#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/web/testname-literal.sh
#
# Two reports ask xymond for statuses of one column with "test=NAME", and
# xymond reads that as a pattern: unanchored, it also takes in every column
# whose name contains NAME (#797).
#
#   - confreport.cgi reads the process and service lists from the "procs" and
#     "svcs" columns, from a host's page, from a page, or for every host. The
#     host also has "myprocs", "mysvcs", "procsx" and "svcsx", each listing a
#     decoy: the report must list the real entries and not the decoys.
#   - statusreport.cgi --column=sslcert, as certreport.cgi runs it: hostb has
#     only a red "mysslcert" and "sslcertx", and must not be in the report;
#     nor with --column='xyz|sslcert', whose names stay whole.
#
# Each page must have been served (exit 0) and show the real entry, so an
# error page cannot pass for a page without the decoy.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
# shellcheck source=tests/lib/xymond-daemon.sh
. "$(dirname "$0")/../lib/xymond-daemon.sh"

ROOT=$(find_root)
require_c_buildenv "$ROOT"
require_bin XYMOND xymond/xymond
require_bin XYMONCLIENT common/xymon
require_bin CONFREPORT web/confreport.cgi
require_bin STATUSREPORT web/statusreport.cgi
require_shm_segments "$(grep -c 'setup_channel(C_[A-Z_]*, CHAN_MASTER)' "$ROOT/xymond/xymond.c")"
require_cfg XYMONSERVER_CFG xymond/etcfiles/xymonserver.cfg

work=$(mktempdir)
mkdir -p "$work/home/tmp" "$work/home/web"
printf 'page p1 P1\n127.0.0.1 hosta.example.com # \n127.0.0.1 hostb.example.com # \n' >"$work/hosts.cfg"
sed -e 's|^XYMONHOME=.*|XYMONHOME="'"$work"'/home"|' \
    -e 's|^XYMONTMP=.*|XYMONTMP="'"$work"'/home/tmp"|' \
	"$XYMONSERVER_CFG" >"$work/xymonserver.cfg"
echo "HOSTSCFG=\"$work/hosts.cfg\"" >>"$work/xymonserver.cfg"

XYMOND_PID=""
stop_xymond() {
	[ -n "$XYMOND_PID" ] || return 0
	kill "$XYMOND_PID" 2>/dev/null || true
	wait "$XYMOND_PID" 2>/dev/null || true
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
x "status hosta,example,com.procs green procs
&green realproc" >/dev/null
x "status hosta,example,com.svcs green svcs
&green realsvc" >/dev/null
# The decoys last: xymond lists a host's newest column first, so a board
# answer with both lists the decoy first, and confreport takes the first
x "status hosta,example,com.myprocs red decoys
&red decoyproc" >/dev/null
x "status hosta,example,com.mysvcs red decoys
&red decoysvc" >/dev/null
# And decoys that only begin with the name, for the closing anchor
x "status hosta,example,com.procsx red decoys
&red decoyproc" >/dev/null
x "status hosta,example,com.svcsx red decoys
&red decoysvc" >/dev/null
x "status hosta,example,com.sslcert red certificate expired" >/dev/null
x "status hostb,example,com.mysslcert red certificate expired" >/dev/null
x "status hostb,example,com.sslcertx red certificate expired" >/dev/null
ready=
for _ in {1..50}; do
	[ "$(x 'xymondboard test=^(procs|svcs|myprocs|mysvcs|procsx|svcsx|sslcert|mysslcert|sslcertx)$ fields=hostname' | wc -l)" -ge 9 ] \
		&& { ready=1; break; }
	sleep 0.1
done
[ -n "$ready" ] || fail "xymond did not take the nine statuses: $(x 'xymondboard fields=hostname,testname' 2>&1)"

# cgi BIN COOKIE ARGS... -- a CGI's page, as a GET; its exit in RC
cgi() {
	local bin=$1 cookie=$2
	shift 2
	set +e
	OUT=$(env REQUEST_METHOD=GET QUERY_STRING="" HTTP_COOKIE="$cookie" XYMONHOME="$work/home" \
		"$bin" --env="$work/xymonserver.cfg" "$@" 2>&1)
	RC=$?
	set -e
}

# From the host's page, from its page p1, and with no cookie: every host
for cookie in "host=hosta.example.com" "pagepath=p1" ""; do
	cgi "$CONFREPORT" "$cookie"
	[ "$RC" -eq 0 ] || fail "confreport.cgi ($cookie) was not served (exit $RC): $OUT"
	assert_contains "realproc" "$OUT" "confreport.cgi ($cookie) does not list the process of the procs column"
	assert_contains "realsvc" "$OUT" "confreport.cgi ($cookie) does not list the service of the svcs column"
	assert_not_contains "decoyproc" "$OUT" "confreport.cgi ($cookie) read the myprocs column as the procs column"
	assert_not_contains "decoysvc" "$OUT" "confreport.cgi ($cookie) read the mysvcs column as the svcs column"
done

cgi "$STATUSREPORT" "" --column=sslcert --filter=color=red,yellow --all --no-colors
[ "$RC" -eq 0 ] || fail "statusreport.cgi --column=sslcert was not served (exit $RC): $OUT"
assert_contains "hosta.example.com" "$OUT" "the sslcert report does not list the host with a red sslcert"
assert_not_contains "hostb.example.com" "$OUT" "the sslcert report lists a host whose red columns are mysslcert and sslcertx"

# A pattern of names stays whole: ^(xyz|sslcert)$, not ^xyz|sslcert$, which
# takes any column ending in sslcert
cgi "$STATUSREPORT" "" '--column=xyz|sslcert' --filter=color=red,yellow --all --no-colors
[ "$RC" -eq 0 ] || fail "statusreport.cgi --column='xyz|sslcert' was not served (exit $RC): $OUT"
assert_contains "hosta.example.com" "$OUT" "--column='xyz|sslcert' does not list the host with a red sslcert"
assert_not_contains "hostb.example.com" "$OUT" "--column='xyz|sslcert' lists the host whose red columns are mysslcert and sslcertx"

pass "confreport.cgi and statusreport.cgi --column take the column named, not every column whose name contains it"
