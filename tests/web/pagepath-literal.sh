#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/web/pagepath-literal.sh
#
# The CGIs that filter by the "pagepath" cookie ask for the page the viewer
# came from and its sub-pages, and the page name was put into that pattern as
# it was, so a '.' in it matched any character (#797); acknowledge.cgi also
# left the first '^' out, and enadis.cgi used the bare name with no anchors,
# so with them the page "ops.eu" took in "devops.eu" as well.
#
# Three pages, each with one host and a red conn: ops.eu, the page the cookie
# names; opsXeu, which an unescaped '.' takes in; and devops.eu, which a
# pattern without its first '^' takes in. With the cookie on ops.eu, a real
# xymond behind them, each CGI must show the host of ops.eu and neither other:
#
#   - acknowledge.cgi --no-pin and confreport.cgi, which ask xymond for the page;
#   - statusreport.cgi, which does too;
#   - enadis.cgi and hostgraphs.cgi, whose forms list the hosts of the page.
#
# Each page must have been served (exit 0) and name the host of ops.eu, so an
# error page cannot pass for a page without the others. A cookie with a tab,
# which cgi_pageregex() refuses, must leave each CGI answering: acknowledge and
# confreport with "Invalid", the other three unfiltered. Each CGI is given 15
# seconds, so one that spins fails the test instead of hanging it.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
# shellcheck source=tests/lib/xymond-daemon.sh
. "$(dirname "$0")/../lib/xymond-daemon.sh"

ROOT=$(find_root)
require_c_buildenv "$ROOT"
require_bin XYMOND xymond/xymond
require_bin XYMONCLIENT common/xymon
require_shm_segments "$(grep -c 'setup_channel(C_[A-Z_]*, CHAN_MASTER)' "$ROOT/xymond/xymond.c")"
# shellcheck source=tests/lib/svcstatus-cgi.sh
. "$(dirname "$0")/../lib/svcstatus-cgi.sh"

# The work tree and the build flags; svcstatus itself is not needed here
svcstatus_setup --no-daemon

cflags=$(xymon_cflags "$ROOT")
ldflags=$(xymon_ldflags "$ROOT")
for cgi in acknowledge confreport statusreport enadis hostgraphs; do
	# shellcheck disable=SC2086  # the flag lists are meant to split
	"$CC" $cflags -o "$work/$cgi" "$ROOT/web/$cgi.c" \
		"$(xymon_lib "$ROOT" libxymon.a)" "$(xymon_lib "$ROOT" libxymoncomm.a)" "$(xymon_lib "$ROOT" libxymon.a)" \
		$pcre_libs $ldflags 2>"$work/cc-$cgi.log" \
		|| { cat "$work/cc-$cgi.log" >&2; fail "$cgi does not compile"; }
done
mkdir -p "$work/web"
cp "$ROOT"/xymond/webfiles/maint_* "$ROOT"/xymond/webfiles/hostgraphs_* "$work/web/"

{
	printf 'page ops.eu Ops EU\n127.0.0.1 hosta.example.com # \n'
	printf 'page opsXeu Ops X\n127.0.0.1 hostb.example.com # \n'
	printf 'page devops.eu DevOps EU\n127.0.0.1 hostc.example.com # \n'
} >"$work/etc/hosts.cfg"
# The templates enadis and hostgraphs show their forms with: the --env file
# wins over the environment, so XYMONHOME goes there
echo "XYMONHOME=\"$work\"" >>"$work/etc/xymonserver.cfg"

mkdir -p "$work/home/etc" "$work/home/tmp" "$work/home/www"
require_cfg XYMONSERVER_CFG xymond/etcfiles/xymonserver.cfg
sed -e 's|^XYMONHOME=.*|XYMONHOME="'"$work"'/home"|' \
    -e 's|^XYMONTMP=.*|XYMONTMP="'"$work"'/home/tmp"|' \
	"$XYMONSERVER_CFG" > "$work/xymond.cfg"

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
		--hosts="$work/etc/hosts.cfg" --env="$work/xymond.cfg" \
		--pidfile="$work/xymond.pid" --checkpoint-file="$work/chk.out" \
		"$@" > "$work/xymond.log" 2>&1 &
	XYMOND_PID=$!
}
start_xymond
echo "XYMONDPORT=\"$PORT\"" >>"$work/etc/xymonserver.cfg"

x() { XYMON_TIMEOUT=5 "$XYMONCLIENT" "127.0.0.1:$PORT" "$1"; }
for h in hosta hostb hostc; do x "status $h,example,com.conn red $h" >/dev/null; done
ready=
for _ in {1..50}; do
	[ "$(x 'xymondboard test=conn fields=hostname' | wc -l)" -ge 3 ] && { ready=1; break; }
	sleep 0.1
done
[ -n "$ready" ] || fail "xymond did not take the three statuses: $(x 'xymondboard fields=hostname,testname' 2>&1)"

# cookie_cgi NAME ARGS... -- a CGI as a GET from a page of ops.eu (or of
# $PAGE), given 15 seconds; its page in OUT and its exit in RC
PAGE=
CGI_PID=
stop_cgi() { [ -z "$CGI_PID" ] || stop_process "$CGI_PID"; }
register_cleanup stop_cgi
cookie_cgi() {
	local cgi=$1 pid
	shift
	env REQUEST_METHOD=GET QUERY_STRING="" HTTP_COOKIE="pagepath=${PAGE:-ops.eu}" \
	    SCRIPT_NAME="$cgi.sh" XYMONHOME="$work" HOSTSCFG="$work/etc/hosts.cfg" \
		"$work/$cgi" --env="$work/etc/xymonserver.cfg" "$@" >"$work/out" 2>/dev/null &
	pid=$!
	CGI_PID=$pid
	for _ in {1..150}; do
		kill -0 "$pid" 2>/dev/null || break
		sleep 0.1
	done
	if kill -0 "$pid" 2>/dev/null; then
		stop_process "$pid"
		fail "$cgi.cgi did not answer within 15 seconds (cookie pagepath=${PAGE:-ops.eu})"
	fi
	RC=0
	wait "$pid" || RC=$?
	CGI_PID=
	OUT=$(cat "$work/out")
}

# only_hosta CGI -- the CGI's page names hosta and neither other host
only_hosta() {
	[ "$RC" -eq 0 ] || fail "$1 was not served (exit $RC)"
	assert_contains "hosta.example.com" "$OUT" "$1 does not show the host of ops.eu"
	assert_not_contains "hostb.example.com" "$OUT" "$1 takes in page opsXeu for the page ops.eu: the '.' matched any character"
	assert_not_contains "hostc.example.com" "$OUT" "$1 takes in page devops.eu for the page ops.eu"
}

cookie_cgi acknowledge --no-pin;	only_hosta acknowledge.cgi
cookie_cgi confreport;			only_hosta confreport.cgi
cookie_cgi statusreport --column=conn;	only_hosta statusreport.cgi
cookie_cgi enadis;			only_hosta enadis.cgi
cookie_cgi hostgraphs;			only_hosta hostgraphs.cgi

# A cookie cgi_pageregex() refuses: an answer from each, never a hang
PAGE=$(printf 'ops\teu')
cookie_cgi acknowledge --no-pin
[ "$RC" -eq 0 ] || fail "acknowledge.cgi with a refused cookie ended with rc $RC"
assert_contains "Invalid pagename filter" "$OUT" "acknowledge.cgi did not say a refused cookie was invalid"
cookie_cgi confreport
assert_contains "Invalid host/page filter" "$OUT" "confreport.cgi did not say a refused cookie was invalid"
for cgi in statusreport enadis hostgraphs; do
	if [ "$cgi" = statusreport ]; then cookie_cgi statusreport --column=conn; else cookie_cgi "$cgi"; fi
	[ "$RC" -eq 0 ] || fail "$cgi.cgi with a refused cookie ended with rc $RC"
	# Every host: a filter split at the tab (page=^ops) would take in hostb
	# too, but never hostc
	for h in hosta hostb hostc; do
		assert_contains "$h.example.com" "$OUT" "$cgi.cgi with a refused cookie does not list every host"
	done
done
PAGE=

pass "acknowledge, confreport, statusreport, enadis and hostgraphs take the pagepath cookie's page and its sub-pages, and no page it only resembles"
