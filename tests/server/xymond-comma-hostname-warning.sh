#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/xymond-comma-hostname-warning.sh
#
# A canonical hostname with a literal comma is not monitored: xymond runs the
# hostname of every status message through uncommafy(), so the results for a
# configured "foo,bar" arrive as "foo.bar" and never match its entry. The
# once-per-load warning must say that, and name the host the results arrive
# as -- not the "it is monitored but its web pages are not reachable" it gives
# for other characters the web cannot serve, which is true only for them.
#
# Pins three things against the built xymond: the comma name gets its own
# warning; it does not get the "it is monitored" one; and a name with other
# punctuation still gets that one. Supporting check: a status sent for the
# comma host does not reach its board entry, which is what "not monitored"
# claims.
#
# Needs a built tree: xymond itself and the xymon client.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
# shellcheck source=tests/lib/xymond-daemon.sh
. "$(dirname "$0")/../lib/xymond-daemon.sh"

require_bin XYMOND xymond/xymond
require_bin XYMONCLIENT common/xymon

command -v sed >/dev/null 2>&1 || skip "sed not available"

work=$(mktempdir)

XYMOND_PID=""
stop_xymond() {
	[ -n "$XYMOND_PID" ] || return 0
	kill "$XYMOND_PID" 2>/dev/null || true
	local i=0
	while kill -0 "$XYMOND_PID" 2>/dev/null && [ "$i" -lt 100 ]; do
		sleep 0.1
		i=$((i+1))
	done
	XYMOND_PID=""
}
register_cleanup stop_xymond

cat > "$work/hosts.cfg" <<'EOF'
page test Test
127.0.0.1	foo,bar		# conn
127.0.0.2	bad!host	# conn
127.0.0.3	goodhost	# conn
EOF

# xymond refuses to start unless XYMONHOME names a real directory; point it
# at the test's own directory, as the other xymond tests do.
mkdir -p "$work/home/etc" "$work/home/tmp" "$work/home/www"
require_cfg XYMONSERVER_CFG xymond/etcfiles/xymonserver.cfg
sed -e 's|^XYMONHOME=.*|XYMONHOME="'"$work"'/home"|' \
    -e 's|^XYMONTMP=.*|XYMONTMP="'"$work"'/home/tmp"|' \
	"$XYMONSERVER_CFG" > "$work/xymonserver.cfg"

xymond_launch() {
	local port=$1; shift
	"$XYMOND" --no-daemon --listen="127.0.0.1:$port" \
		--hosts="$work/hosts.cfg" --env="$work/xymonserver.cfg" \
		--pidfile="$work/xymond.pid" --checkpoint-file="$work/chk" "$@" \
		> "$work/xymond.log" 2>&1 &
	XYMOND_PID=$!
}

start_xymond

# The warnings are written when the configuration loads, before the daemon
# answers; wait for them rather than for a duration all the same.
i=0
while ! grep -qE "Warning: hostname '(bad!host|foo,bar)'" "$work/xymond.log" \
		&& [ "$i" -lt 50 ]; do
	sleep 0.1
	i=$((i+1))
done
log=$(cat "$work/xymond.log")

assert_contains "hostname 'foo,bar' in hosts.cfg contains a comma" "$log" \
	"the comma name gets its own warning"
assert_contains "arrive as 'foo.bar' and the host is not monitored" "$log" \
	"the warning names the host the results arrive as, and says it is not monitored"
assert_not_contains "hostname 'foo,bar' in hosts.cfg has characters the web interface cannot serve" "$log" \
	"the comma name does not get the 'it is monitored' warning"
assert_contains "hostname 'bad!host' in hosts.cfg has characters the web interface cannot serve; it is monitored" "$log" \
	"other punctuation keeps the 'it is monitored' warning"
assert_not_contains "hostname 'goodhost'" "$log" \
	"a plain name draws no warning"

# Supporting check: what the new warning claims. The host part of a status
# message spells dots as commas, so this is how a client reports "foo,bar".
# goodhost is the sentinel: once its status is on the board, the comma
# host's status has been processed too.
"$XYMONCLIENT" "127.0.0.1:$PORT" "status foo,bar.conn green up" \
	|| fail "cannot send a status to xymond"
"$XYMONCLIENT" "127.0.0.1:$PORT" "status goodhost.conn green up" \
	|| fail "cannot send a status to xymond"
i=0
until grep -q "^goodhost|conn" <<<"$("$XYMONCLIENT" "127.0.0.1:$PORT" "xymondboard host=goodhost fields=hostname,testname" 2>/dev/null)"; do
	[ "$i" -lt 50 ] || fail "the sentinel status never reached the board"
	sleep 0.1
	i=$((i+1))
done
board=$("$XYMONCLIENT" "127.0.0.1:$PORT" "xymondboard fields=hostname,testname" 2>/dev/null || true)
assert_not_contains "foo,bar|conn" "$board" \
	"a status sent for the comma host does not reach its entry"

pass "a comma hostname is warned about as not monitored, other punctuation as web-unreachable"
