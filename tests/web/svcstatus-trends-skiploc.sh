#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/web/svcstatus-trends-skiploc.sh
#
# With --locator, the trends page of a host whose RRDs live elsewhere
# redirects the browser to the server the locator names. That server runs
# the same svcstatus with the same locator, so without a guard it redirected
# again, and the browser looped. The redirect now carries SKIPLOC=1, and a
# request carrying exactly SKIPLOC=1 renders locally instead of asking the
# locator: one hop, never two.
#
# Runs the real CGI against a real xymond_locator, with an rrd server
# registered through lib/locator, as svcstatus-locator-client-link.sh does.
# The redirect is built from the locator's extras, so this needs xymond_locator
# to return them whole (#589).

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

# The trends page looks the host up through xymond before it redirects: a
# fake xymond answers the lookup, as in svcstatus-histlog-serving.sh.
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

# An rrd server with its CGI URL as the locator extra, and realhost on it.
printf '%s\n' \
	"r s 10.0.0.9:1988 rrd 2 1 http://worker.example/xymon-cgi" \
	"r h 10.0.0.9:1988 rrd realhost" \
	| "$LOCATOR" "127.0.0.1:$LPORT" > "$work/register.log" 2>&1
[ "$(grep -c '^>*OK$' "$work/register.log")" -ge 2 ] \
	|| fail "registering the rrd server failed: $(cat "$work/register.log")"

redirect="Location: http://worker.example/xymon-cgi/svcstatus.sh?HOST=realhost&SERVICE=trends"

# The first hop: redirected to the server the locator names, marked as such.
render_live "HOST=realhost&SERVICE=trends" "--locator=127.0.0.1:$LPORT"
assert_contains "$redirect&SKIPLOC=1" "$OUT" \
	"the trends page did not redirect to the rrd server with SKIPLOC=1"

# The second hop: the request the redirect produced renders here, so the
# browser does not loop.
render_live "HOST=realhost&SERVICE=trends&SKIPLOC=1" "--locator=127.0.0.1:$LPORT"
assert_not_contains "Location:" "$OUT" \
	"a trends request carrying SKIPLOC=1 was redirected again: the browser would loop"

# Only exactly SKIPLOC=1 skips the locator.
render_live "HOST=realhost&SERVICE=trends&SKIPLOC=2" "--locator=127.0.0.1:$LPORT"
assert_contains "$redirect&SKIPLOC=1" "$OUT" \
	"SKIPLOC=2 skipped the locator, though only SKIPLOC=1 is the guard"

pass "with --locator, the trends page redirects once, with SKIPLOC=1, and a request carrying SKIPLOC=1 renders locally"
