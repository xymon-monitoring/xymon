#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/web/svcstatus-locator-client-link.sh
#
# With --locator, a historical status page must link the client data saved
# with it, on the server that holds it.
#
# The saved client data lives with xymond_hostdata, which in a distributed
# set-up runs on another server; svcstatus asks xymond_locator which one and
# links "<its CGI URL>/historylog.sh?CLIENT=...&TIMEBUF=...". That branch
# built the link but never set clientavail, so the page never showed it; and
# it named svcstatus.sh, which shows the host's current client data rather
# than the saved copy -- only the historical view, which the CGI wrapper
# selects by the name historylog.sh, reads that.
#
# Runs the real CGI against a real xymond_locator, with a hostdata server
# registered through lib/locator, the interactive client built beside it.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)

require_c_buildenv "$ROOT"
require_bin XYMOND_LOCATOR xymond/xymond_locator
require_bin LOCATOR lib/locator
# shellcheck source=tests/lib/svcstatus-cgi.sh
. "$(dirname "$0")/../lib/svcstatus-cgi.sh"

svcstatus_setup --no-daemon
svcstatus_build || { cat "$work/cc.log" >&2; fail "svcstatus does not build"; }

# The historical page looks the host up through xymond and renders with the
# histlog templates: a fake xymond answers the lookup, as in
# svcstatus-histlog-serving.sh.
mkdir -p "$work/web"
cp "$ROOT/xymond/webfiles/histlog_header" "$ROOT/xymond/webfiles/histlog_footer" "$work/web/"
printf 'XMH_IP:127.0.0.1\n' >"$work/hostinfo.reply"
"$CC" -o "$work/fake-xymond" "$ROOT/tests/lib/fake-xymond.c" 2>"$work/cc-fake.log" \
	|| { cat "$work/cc-fake.log" >&2; fail "fake-xymond responder does not compile"; }
"$work/fake-xymond" "$work/hostinfo.reply" >"$work/fake-xymond.port" &
fakepid=$!
register_cleanup "kill $fakepid 2>/dev/null || true"
for _ in 1 2 3 4 5 6 7 8 9 10; do
	[ -s "$work/fake-xymond.port" ] && break
	sleep 0.2
done
[ -s "$work/fake-xymond.port" ] || fail "fake-xymond did not report its port"
echo "XYMONDPORT=\"$(cat "$work/fake-xymond.port")\"" >>"$work/etc/xymonserver.cfg"

mkdir -p "$work/tmp"
export XYMONTMP="$work/tmp"

# answers PORT -- the locator on PORT replies to a ping.
answers() {
	grep -q "Locator is available" <<<"$("$LOCATOR" "127.0.0.1:$1" </dev/null 2>/dev/null)"
}

# Start the locator on a free UDP port; a taken one makes it exit at once,
# which is a retry on another port.
lpid= LPORT=
for try in 1 2 3 4 5 6 7 8 9 10; do
	p=$(( 20000 + (RANDOM % 20000) ))
	"$XYMOND_LOCATOR" --listen="127.0.0.1:$p" --logfile="$work/locator.log" &
	cand=$!
	up=
	for _ in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do
		kill -0 "$cand" 2>/dev/null || break
		if answers "$p"; then up=1; break; fi
		sleep 0.1
	done
	if [ -n "$up" ]; then lpid=$cand; LPORT=$p; break; fi
	kill "$cand" 2>/dev/null || true
	wait "$cand" 2>/dev/null || true
done
[ -n "$lpid" ] || fail "xymond_locator did not come up: $(cat "$work/locator.log" 2>/dev/null)"
register_cleanup "kill $lpid 2>/dev/null || true"

# A hostdata server with its CGI URL as the locator extra, and realhost
# assigned to it.
printf '%s\n' \
	"r s 10.0.0.9:1988 hostdata 2 1 http://worker.example/xymon-cgi" \
	"r h 10.0.0.9:1988 hostdata realhost" \
	| "$LOCATOR" "127.0.0.1:$LPORT" > "$work/register.log" 2>&1
[ "$(grep -c '^>*OK$' "$work/register.log")" -ge 2 ] \
	|| fail "registering the hostdata server failed: $(cat "$work/register.log")"

# A history entry carrying the ID of the client data saved with it.
tb="Sun_Oct_4_12:38:53_2026"
mkdir -p "$work/var/histlogs/realhost/disk"
printf 'red Sun Oct  4 12:38:53 UTC 2026 disk is red\nMessage received from 127.0.0.1\nClient data ID 1791117524\n' \
	> "$work/var/histlogs/realhost/disk/$tb"

render "HOST=realhost&SERVICE=disk&TIMEBUF=$tb" "--locator=127.0.0.1:$LPORT"
[ "$RC" -eq 0 ] || fail "the historical page was refused: $OUT"
assert_contains "http://worker.example/xymon-cgi/historylog.sh?CLIENT=realhost&amp;TIMEBUF=1791117524" "$OUT" \
	"the historical page does not link the saved client data on the server the locator named"

# Contrast: without --locator the data is looked for here, and there is none,
# so no link -- proves the link above comes from the locator branch.
render "HOST=realhost&SERVICE=disk&TIMEBUF=$tb"
assert_not_contains "CLIENT=realhost" "$OUT" \
	"a client data link appeared without --locator, for client data this server does not hold"

pass "with --locator, a historical page links the saved client data via historylog.sh on the server holding it"
