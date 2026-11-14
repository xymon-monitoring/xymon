#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/xymonproxy-servers.sh
#
# xymonproxy keeps at most three --server addresses (MAX_SERVERS), as
# xymonproxy.8 says. It stored each one in a table of that size without a
# bound, so a fourth was written past it. The rest are now refused with a
# message, and the first three are used.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
# shellcheck source=tests/lib/xymond-daemon.sh
. "$(dirname "$0")/../lib/xymond-daemon.sh"

require_bin XYMONPROXY xymonproxy/xymonproxy
require_bin XYMONCLIENT common/xymon	# free_port() probes with it

work=$(mktempdir)
"$XYMONPROXY" --no-daemon --listen="127.0.0.1:$(free_port)" \
	--server="127.0.0.1:1,127.0.0.1:2,127.0.0.1:3,127.0.0.1:4,127.0.0.1:5" >"$work/proxy.log" 2>&1 &
pid=$!
register_cleanup "stop_process $pid"
for _ in {1..50}; do
	grep -q 'Sending to Xymon server' "$work/proxy.log" 2>/dev/null && break
	kill -0 "$pid" 2>/dev/null || break
	sleep 0.1
done
log=$(cat "$work/proxy.log")
kill -0 "$pid" 2>/dev/null || fail "xymonproxy stopped with five --server addresses: $log"
assert_contains "Only 3 servers are used, 127.0.0.1:4 and the rest are ignored" "$log" "a fourth server was not refused"
# The whole list, so a fourth kept as well shows
used=$(sed -n 's/.*Sending to Xymon server(s) //p' "$work/proxy.log")
[ "$used" = "127.0.0.1:1 127.0.0.1:2 127.0.0.1:3 " ] \
	|| fail "xymonproxy sends to '$used', not the first three servers alone"

pass "xymonproxy uses the first three --server addresses and refuses the rest with a message"
