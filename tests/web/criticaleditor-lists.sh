#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/web/criticaleditor-lists.sh
#
# The critical systems editor offers what can be chosen, as well as taking
# what is typed. With a real xymond behind criticaleditor.cgi, checked:
#
#   - the host field offers the hosts in hosts.cfg, sorted, and holds the one
#     being edited;
#   - the test field offers the tests that host reports to xymond, without
#     the columns xymond makes up (info, trends), and holds the one being
#     edited;
#   - a host xymond has no tests for offers none, and the test is typed;
#   - a name that is no host in hosts.cfg, as a template for clones is, is
#     kept and saved, with a note saying so;
#   - choosing another host clears the test chosen for the last one;
#   - the table lists the configured critical systems, each with a link to
#     edit it, its monitoring time in words, and its group escaped; with none
#     configured it says how to add one;
#   - what is shown in the fields, the lists and the table is escaped: a host
#     name, a test, a group and a monitoring time the editor cannot read,
#     each carrying markup, from the request or from critical.cfg; the edit
#     link's parameters are URL-encoded. Each escape has an assertion that
#     fails without it.

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
# A name with a quote in it, for the escaping of the host list's options
printf "127.0.0.1 o'brien.example.com # \n" >>"$work/etc/hosts.cfg"

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
	# What each field holds, and what its list offers
	HOST=$(sed -n 's/.*NAME="HOSTNAME"[^>]*VALUE="\([^"]*\)".*/\1/p' <<<"$OUT")
	SVC=$(sed -n 's/.*NAME="SERVICE"[^>]*VALUE="\([^"]*\)".*/\1/p' <<<"$OUT")
	HOSTOPTS=$(sed -n '/<DATALIST ID="critedit-hosts">/,/<\/DATALIST>/s/.*<option value="\([^"]*\)">.*/\1/p' <<<"$OUT" | tr '\n' ';')
	SVCOPTS=$(sed -n '/<DATALIST ID="critedit-tests">/,/<\/DATALIST>/s/.*<option value="\([^"]*\)">.*/\1/p' <<<"$OUT" | tr '\n' ';')
}

printf 'alpha.example.com|conn|||W:0700:2000|1|<b>Unix</b>||someone 2026-11-10 12:00:00\n' >"$work/etc/critical.cfg"

edit "HOSTNAME=alpha.example.com&SERVICE=conn&Find=Show"
[ "$HOSTOPTS" = "alpha.example.com;charlie.example.com;mail.example.com;o&#39;brien.example.com;" ] \
	|| fail "the host list reads '$HOSTOPTS', not hosts.cfg's hosts sorted"
[ "$HOST/$SVC" = "alpha.example.com/conn" ] || fail "the form holds '$HOST/$SVC', not alpha.example.com/conn"
[ "$SVCOPTS" = "conn;cpu;disk;" ] \
	|| fail "the test list reads '$SVCOPTS', not alpha's tests without info and trends"
assert_not_contains "Not a host in hosts.cfg" "$OUT" "a host in hosts.cfg was said not to be"
assert_contains 'criticaleditor.sh?HOSTNAME=alpha%2eexample%2ecom&amp;SERVICE=conn&amp;Find=Show' "$OUT" "the table has no link to edit alpha's conn"
assert_contains "Mon-Fri 07:00-20:00" "$OUT" "the table does not show the monitoring time in words"
assert_contains "&lt;b&gt;Unix&lt;/b&gt;" "$OUT" "the table does not escape the resolver group"

edit "HOSTNAME=charlie.example.com&Find=Show"
assert_contains '<INPUT NAME="SERVICE" TYPE="TEXT"' "$OUT" "a host with no tests in xymond has no test field to type in"
[ -z "$SVCOPTS" ] || fail "a host with no tests in xymond was offered tests: '$SVCOPTS'"

# the form was on alpha/conn, and mail was chosen in the host field
edit "HOSTNAME=mail.example.com&SERVICE=conn&PREVHOST=alpha.example.com&Find=Show"
[ -z "$SVC" ] || fail "choosing another host kept the old test: the test field holds '$SVC'"
[ "$SVCOPTS" = "smtp;" ] || fail "mail's test list reads '$SVCOPTS', not smtp"

# A template for clones is a name no host has: typed, kept, and saved
edit "HOSTNAME=Standard%20Unix&SERVICE=cpu&PRIORITY=2&Update=1"
assert_contains "Saved" "$OUT" "a template could not be saved"
grep -q '^Standard Unix|cpu|' "$work/etc/critical.cfg" || fail "the template is not in critical.cfg: $(cat "$work/etc/critical.cfg")"
[ "$HOST" = "Standard Unix" ] || fail "after saving a template, the host field holds '$HOST'"
assert_contains "Not a host in hosts.cfg: a template for clones" "$OUT" "a name no host has was not marked as such"
assert_contains '>Standard Unix</A>' "$OUT" "the table does not list the template"

# What the fields and the table show is escaped
printf '%s\n' "x\"><b>.example.com|conn||||1|g\"r'p<i>||" >>"$work/etc/critical.cfg"
edit "HOSTNAME=x%22%3E%3Cb%3E.example.com&SERVICE=conn&Find=Show"
assert_contains 'VALUE="x&quot;&gt;&lt;b&gt;.example.com"' "$OUT" "the host field does not escape its value"
assert_contains '>x&quot;&gt;&lt;b&gt;.example.com</A>' "$OUT" "the table does not escape a host"
assert_contains "g&quot;r&#39;p&lt;i&gt;" "$OUT" "the table does not escape a group"
assert_not_contains '<b>.example.com' "$OUT" "a host name reached the page as markup"
assert_not_contains "r'p<i>" "$OUT" "a group reached the page as markup"
assert_not_contains "o'brien" "$OUT" "the host list does not escape its options"

# A test from the request, and a test and a monitoring time from critical.cfg
printf '%s\n' 'alpha.example.com|t"><i>|||<u>odd|1|||' >>"$work/etc/critical.cfg"
edit "HOSTNAME=alpha.example.com&SERVICE=x%22%3E%3Cb%3EPWNED%3C%2Fb%3E&Find=Show"
[ "$SVC" = "x&quot;&gt;&lt;b&gt;PWNED&lt;/b&gt;" ] || fail "the test field does not escape its value: '$SVC'"
assert_not_contains "<b>PWNED" "$OUT" "a test from the request reached the page as markup"
assert_contains '<TD>t&quot;&gt;&lt;i&gt;</TD>' "$OUT" "the table does not escape a test"
assert_contains '&amp;SERVICE=t%22%3e%3ci%3e&amp;Find=Show' "$OUT" "the edit link does not URL-encode a test"
assert_not_contains 't"><i>' "$OUT" "a test from critical.cfg reached the page as markup"
assert_contains '&lt;u&gt;odd' "$OUT" "the table does not escape a monitoring time it cannot read"
assert_not_contains '<u>odd' "$OUT" "a monitoring time reached the page as markup"

: >"$work/etc/critical.cfg"
edit "Find=Show"
assert_contains "None yet: choose a host and a test below" "$OUT" "an empty configuration does not say how to add one"

pass "the critical systems editor lists the hosts, the host's reporting tests, and the configured systems to choose from"
