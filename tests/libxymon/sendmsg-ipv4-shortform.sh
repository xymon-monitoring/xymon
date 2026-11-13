#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/libxymon/sendmsg-ipv4-shortform.sh
#
# The client reaches a recipient given as an IPv4 short form (lib/sendmsg.c).
#
# 127.1 is 127.0.0.1, as inet_aton() and the listeners read it. The client
# passed the recipient straight to getaddrinfo(), which on OpenBSD reads only
# a dotted quad, so 127.1:port could not be reached there. Now an
# inet_aton()-readable address is canonicalised before getaddrinfo().
#
# The real xymon client queries tests/lib/fake-xymond.c, listening on
# 127.0.0.1, addressed as the short form 127.1, and must print its reply.
# Only OpenBSD's resolver rejects the short form, so this reddens there alone.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
require_bin XYMON common/xymon
require_cc

work=$(mktempdir)
pids=""
register_cleanup 'stop_process $pids'

"$CC" -o "$work/fake-xymond" "$ROOT/tests/lib/fake-xymond.c" 2>"$work/cc.log" \
	|| fail "fake-xymond does not compile: $(cat "$work/cc.log")"
printf 'reply from the fake xymond\n' >"$work/reply"

"$work/fake-xymond" "$work/reply" 127.0.0.1 >"$work/port" 2>"$work/port.err" &
pids="$pids $!"
for _ in $(seq 1 50); do [ -s "$work/port" ] && break; kill -0 "$!" 2>/dev/null || break; sleep 0.1; done
[ -s "$work/port" ] || fail "fake-xymond cannot listen on 127.0.0.1: $(cat "$work/port.err")"
port=$(cat "$work/port")

got=$(env XYMONDPORT=1984 "$XYMON" "127.1:$port" "query sendmsg.test" 2>"$work/client.err" || true)
[ "$got" = "reply from the fake xymond" ] \
	|| fail "the client did not reach 127.1:$port, the short form of 127.0.0.1 (printed '$got'; $(cat "$work/client.err"))"

pass "the client reaches a recipient given as the IPv4 short form 127.1"
