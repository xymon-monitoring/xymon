#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/xymonnet/contest-tls.sh
#
# The standalone contest, which "make install-tools" installs to run one
# network test by hand, must speak TLS for a TLS service the way xymonnet
# does. Its build rule compiled contest.c without $(SSLFLAGS), so
# HAVE_OPENSSL was unset, contest.h defined TCP_SSL as 0 and every SSL call
# was a stub: an imaps test sent its "ABC123 LOGOUT" in clear text.
#
# The peer sends nothing and records the first byte it receives. A TLS
# client opens with a handshake record (0x16); the clear-text one sends
# the service's own text ("A", 0x41, for imaps). The service definition
# is the shipped [imaps] entry, from a copy of protocols.cfg.

set -euo pipefail
. "$(dirname "$0")/../lib/assert.sh"
root=$(find_root)

require_bin CONTEST xymonnet/contest
require_cc

work=$(mktempdir); register_cleanup "rm -rf '$work'"
mkdir -p "$work/home/etc"
cp "$root/xymonnet/protocols.cfg" "$work/home/etc/protocols.cfg"

"$CC" -o "$work/peer" "$root/tests/xymonnet/contest-tls-harness.c" 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; skip "contest-tls-harness does not compile"; }

"$work/peer" "$work/verdict.txt" > "$work/port" &
peer=$!
register_cleanup "kill $peer 2>/dev/null || :"

i=0
while [ "$i" -lt 50 ]; do
	[ -s "$work/port" ] && break
	kill -0 "$peer" 2>/dev/null || fail "the peer exited before naming its port"
	sleep 0.1
	i=$((i + 1))
done
port=$(cat "$work/port")
[ -n "$port" ] || fail "the peer never named a port"

XYMONHOME="$work/home" "$CONTEST" --timeout=8 "127.0.0.1/$port/imaps" \
	>"$work/out.txt" 2>&1 || :
wait "$peer" 2>/dev/null || :

[ -s "$work/verdict.txt" ] || fail "the peer recorded nothing -- contest never connected:
$(cat "$work/out.txt")"
verdict=$(cat "$work/verdict.txt")

[ "$verdict" = "16" ] || fail \
	"contest opened an imaps test without a TLS handshake: the peer's first byte was [$verdict], expected [16]
-- contest built without \$(SSLFLAGS) has every SSL call stubbed out (xymonnet/Makefile, the contest rule)"

pass "the standalone contest opens a TLS service test with a TLS handshake"
