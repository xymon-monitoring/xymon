#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/web/hostname-literal.sh
#
# The CGIs ask xymond for one host's statuses with "host=^NAME$", and the
# dots of NAME matched any character (#758). With web01.example.com and
# web01-example.com both configured, a red "decoy" status on the second, a
# red "conn" on the first, and a real xymond behind the CGIs, each page for
# web01.example.com is checked to show its own status and not the decoy:
#
#   - svcstatus.cgi, the info page (svcstatus-info.c);
#   - svcstatus.cgi, a COMPACT column (summary=conn|decoy);
#   - acknowledge.cgi --no-pin, filtered by the "host" cookie;
#   - confreport.cgi, filtered by the "host" cookie.
#
# Each page must have been served (exit 0) and show "conn", so an error page
# cannot pass for a page without the decoy. web01-example.com sorts first,
# so a board answer holding both hosts lists the wrong one's rows first.

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

svcstatus_setup --no-daemon
svcstatus_build || { cat "$work/cc.log" >&2; fail "svcstatus does not build"; }

# acknowledge.cgi and confreport.cgi, from their sources, as svcstatus is
cflags=$(xymon_cflags "$ROOT")
ldflags=$(xymon_ldflags "$ROOT")
for cgi in acknowledge confreport; do
	# shellcheck disable=SC2086  # the flag lists are meant to split
	"$CC" $cflags -o "$work/$cgi" "$ROOT/web/$cgi.c" \
		"$(xymon_lib "$ROOT" libxymon.a)" "$(xymon_lib "$ROOT" libxymoncomm.a)" "$(xymon_lib "$ROOT" libxymon.a)" \
		$pcre_libs $ldflags 2>"$work/cc-$cgi.log" \
		|| { cat "$work/cc-$cgi.log" >&2; fail "$cgi does not compile"; }
done

{
	printf '127.0.0.1 web01-example.com # \n'
	printf '127.0.0.1 web01.example.com # COMPACT:summary=conn|decoy\n'
} >"$work/etc/hosts.cfg"

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
		"$@" \
		> "$work/xymond.log" 2>&1 &
	XYMOND_PID=$!
}

start_xymond
echo "XYMONDPORT=\"$PORT\"" >>"$work/etc/xymonserver.cfg"

x() { XYMON_TIMEOUT=5 "$XYMONCLIENT" "127.0.0.1:$PORT" "$1"; }

x "status web01-example,com.decoy red the other host" >/dev/null
x "status web01,example,com.conn red this host" >/dev/null

# the board itself answers for both: this is what the pages were handed
got=
for _ in $(seq 1 50); do
	got=$(x 'xymondboard host=^web01.example.com$ fields=hostname,testname')
	case $got in *"web01-example.com|decoy"*"web01.example.com|conn"*) break ;; esac
	sleep 0.1
done
case $got in *"web01-example.com|decoy"*) ;; *) fail "the setup does not reproduce #758: the board answer is '$got'" ;; esac

# cookie_cgi NAME ARGS... -- run a cookie-filtered CGI for web01.example.com
cookie_cgi() {
	local cgi=$1
	shift
	set +e
	OUT=$(env REQUEST_METHOD=GET QUERY_STRING="" HTTP_COOKIE="host=web01.example.com" \
	      SCRIPT_NAME="$cgi.sh" XYMONHOME="$work" \
		"$work/$cgi" --env="$work/etc/xymonserver.cfg" "$@" 2>/dev/null)
	RC=$?
	set -e
}

render_live "HOST=web01.example.com&SERVICE=info"
[ "$RC" -eq 0 ] || fail "the info page was not served (exit $RC)"
assert_contains "conn" "$OUT" "the info page does not list the host's own test"
assert_not_contains "decoy" "$OUT" "the info page lists a test of web01-example.com"

render_live "HOST=web01.example.com&SERVICE=summary"
[ "$RC" -eq 0 ] || fail "the COMPACT column was not served (exit $RC)"
assert_contains "conn" "$OUT" "the COMPACT column does not show the host's own conn status"
assert_not_contains "decoy" "$OUT" "the COMPACT column is built from web01-example.com's decoy status"

cookie_cgi acknowledge --no-pin
[ "$RC" -eq 0 ] || fail "acknowledge.cgi was not served (exit $RC)"
assert_contains ">conn<" "$OUT" "acknowledge.cgi does not offer the host's own red conn"
assert_not_contains "decoy" "$OUT" "acknowledge.cgi offers web01-example.com's decoy for the host cookie web01.example.com"

cookie_cgi confreport
[ "$RC" -eq 0 ] || fail "confreport.cgi was not served (exit $RC)"
assert_contains "web01.example.com" "$OUT" "confreport.cgi does not report the host of the cookie"
assert_not_contains "web01-example.com" "$OUT" "confreport.cgi reports web01-example.com for the host cookie web01.example.com"

pass "the info page, a COMPACT column, acknowledge.cgi and confreport.cgi show the host's own statuses, not a near-named host's"
