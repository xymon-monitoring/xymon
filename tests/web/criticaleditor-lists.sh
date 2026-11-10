#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/web/criticaleditor-lists.sh
#
# The critical systems editor offers what can be chosen, instead of asking
# for it to be typed. With a real xymond behind criticaleditor.cgi, checked:
#
#   - the host pull-down lists the hosts in hosts.cfg, sorted, with the one
#     being edited selected;
#   - the test pull-down lists the tests that host reports to xymond, without
#     the columns xymond makes up (info, trends), with the one being edited
#     selected;
#   - a host xymond has no tests for gets a text box instead;
#   - choosing another host clears the test chosen for the last one;
#   - the table lists the configured critical systems, each with a link to
#     edit it, its monitoring time in words, and its group escaped; with none
#     configured it says how to add one.

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

mkdir -p "$work/web" "$work/etc" "$work/home/etc" "$work/home/tmp" "$work/home/www"
cp "$ROOT"/xymond/webfiles/critedit_* "$work/web/"
printf '127.0.0.1 mail.example.com # \n127.0.0.1 alpha.example.com # \n127.0.0.1 charlie.example.com # \n' >"$work/etc/hosts.cfg"

# A real xymond, so the editor can ask which tests each host reports
P=$(free_port)
require_cfg XYMONSERVER_CFG xymond/etcfiles/xymonserver.cfg
sed -e 's|^XYMONHOME=.*|XYMONHOME="'"$work"'/home"|' \
    -e 's|^XYMONTMP=.*|XYMONTMP="'"$work"'/home/tmp"|' \
	"$XYMONSERVER_CFG" >"$work/xymond.cfg"
MLPID=
"$XYMOND" --no-daemon --listen="127.0.0.1:$P" \
	--hosts="$work/etc/hosts.cfg" --env="$work/xymond.cfg" \
	--pidfile="$work/xymond.pid" --checkpoint-file="$work/chk.out" \
	>"$work/xymond.log" 2>&1 &
MLPID=$!
stop() { [ -n "$MLPID" ] || return 0; kill "$MLPID" 2>/dev/null || true; wait "$MLPID" 2>/dev/null || true; MLPID=; }
register_cleanup 'stop'
for _ in $(seq 1 100); do
	grep -q 'Setup complete' "$work/xymond.log" 2>/dev/null && break
	kill -0 "$MLPID" 2>/dev/null || fail "xymond did not start: $(cat "$work/xymond.log")"
	sleep 0.1
done
grep -q 'Setup complete' "$work/xymond.log" || fail "xymond did not start: $(cat "$work/xymond.log")"

x() { XYMON_TIMEOUT=5 "$XYMONCLIENT" "127.0.0.1:$P" "$1" >/dev/null; }
x "status alpha.example.com.disk green ok"
x "status alpha.example.com.conn green ok"
x "status alpha.example.com.cpu green ok"
x "status mail.example.com.smtp green ok"
sleep 0.5

edit() {
	OUT=$(REQUEST_METHOD=GET QUERY_STRING="$1" XYMONHOME="$work" \
		HOSTSCFG="!$work/etc/hosts.cfg" XYMSRV="127.0.0.1" XYMONDPORT="$P" \
		"$work/criticaleditor" --config="$work/etc/critical.cfg" 2>/dev/null) \
		|| fail "criticaleditor failed (rc $?) for '$1'"
	HOSTOPTS=$(sed -n '/NAME="HOSTNAME"/,/<\/SELECT>/s/.*<option value="\([^"]*\)"\( selected\)*>.*/\1\2/p' <<<"$OUT" | tr '\n' ';')
	SVCOPTS=$(sed -n '/<SELECT NAME="SERVICE"/,/<\/SELECT>/s/.*<option value="\([^"]*\)"\( selected\)*>.*/\1\2/p' <<<"$OUT" | tr '\n' ';')
}

printf 'alpha.example.com|conn|||W:0700:2000|1|<b>Unix</b>||someone 2026-11-10 12:00:00\n' >"$work/etc/critical.cfg"

edit "HOSTNAME=alpha.example.com&SERVICE=conn&Find=Show"
[ "$HOSTOPTS" = ";alpha.example.com selected;charlie.example.com;mail.example.com;" ] \
	|| fail "the host pull-down reads '$HOSTOPTS', not hosts.cfg's hosts sorted with alpha selected"
[ "$SVCOPTS" = "conn selected;cpu;disk;" ] \
	|| fail "the test pull-down reads '$SVCOPTS', not alpha's tests (no info/trends) with conn selected"
assert_contains 'criticaleditor.sh?HOSTNAME=alpha%2eexample%2ecom&amp;SERVICE=conn&amp;Find=Show' "$OUT" "the table has no link to edit alpha's conn"
assert_contains "Mon-Fri 07:00-20:00" "$OUT" "the table does not show the monitoring time in words"
assert_contains "&lt;b&gt;Unix&lt;/b&gt;" "$OUT" "the table does not escape the resolver group"

edit "HOSTNAME=charlie.example.com&Find=Show"
assert_contains '<INPUT NAME="SERVICE" TYPE="TEXT"' "$OUT" "a host with no tests in xymond did not get a text box"
[ -z "$SVCOPTS" ] || fail "a host with no tests in xymond got a test pull-down: '$SVCOPTS'"

# the form was on alpha/conn, and mail was chosen from the host pull-down
edit "HOSTNAME=mail.example.com&SERVICE=conn&PREVHOST=alpha.example.com&Find=Show"
[ "$SVCOPTS" = " selected;smtp;" ] \
	|| fail "choosing another host kept the old test: the test pull-down reads '$SVCOPTS'"

: >"$work/etc/critical.cfg"
edit "Find=Show"
assert_contains "None yet: choose a host and a test below" "$OUT" "an empty configuration does not say how to add one"

pass "the critical systems editor lists the hosts, the host's reporting tests, and the configured systems to choose from"
