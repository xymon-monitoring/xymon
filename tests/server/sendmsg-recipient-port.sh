#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/sendmsg-recipient-port.sh
#
# Each recipient in XYMSERVERS without an explicit port gets the default port
# of its OWN transport, not the first recipient's. setup_transport() in
# lib/sendmsg.c used to compute one default from the first recipient and apply
# it to every later one: with an http:// server listed first, a plain server
# after it was sent to port 80 instead of XYMONDPORT -- silently, so its
# messages were lost.
#
# Driven through the xymon CLI with a multi-recipient command (status), which
# goes to every XYMSERVERS entry; --debug prints the port chosen per recipient
# before it connects, so no listener is needed. The servers are unreachable.
# It lives under tests/server/ because the xymon CLI is a server-build tool
# (a client-only build does not provide common/xymon).

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin XYMON common/xymon

# run XYMSERVERS -- the CLI's --debug output for a status sent to all of them
run() {
	XYMONDPORT=19999 XYMSERVERS="$1" \
		"$XYMON" --debug 0.0.0.0 "status test.test green hi" 2>&1 || true
}

# http first, plain second: the plain server must use XYMONDPORT (19999), not
# the http transport's default of 80.
out=$(run "http://127.0.0.1:19998/xymon-cgi/xymoncgimsg.cgi 127.0.0.1")
assert_contains "Standard protocol on port 19999" "$out" \
	"the plain recipient did not use XYMONDPORT after an http recipient"
assert_not_contains "Standard protocol on port 80" "$out" \
	"the plain recipient was sent to the http default port 80"

# A plain recipient's own ":port" is still honoured, alongside a port-less one.
out=$(run "127.0.0.1 127.0.0.1:12345")
assert_contains "Standard protocol on port 19999" "$out" \
	"the port-less plain recipient did not use XYMONDPORT"
assert_contains "Standard protocol on port 12345" "$out" \
	"an explicit :port on a recipient was not honoured"

pass "each XYMSERVERS recipient uses its own transport's default port, not the first recipient's"
