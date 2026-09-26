#!/usr/bin/env bash
#
# The companion to tls-handshake-wait.sh, which reads the source. This one
# runs the probe against a peer that accepts the connection and then says
# nothing, so SSL_connect() never completes, and measures what waiting costs.
#
# The property is not speed. The handshake takes as long as the peer takes
# either way -- over 20 paired runs the wall-clock difference was +0.30s
# with a 95% CI of -0.34..+0.93, i.e. nothing. What changes is CPU: spinning
# on SSL_connect() burned 6.8s on a 6.3s run, saturating a core for the whole
# stall, and it scaled with the wait (6.8s at --timeout=5, 11.7s at 10).
# Blocking in select() costs ~0.
#
# So the assertion is a CPU ceiling far below the timeout. A source test
# cannot express that: which fd set the socket joins is visible in the code,
# but whether the process then sleeps or spins is only visible by running it.

set -eu
. "$(dirname "$0")/../lib/assert.sh"
root=$(find_root)

require_bin XYMONNET xymonnet/xymonnet
require_cc

work=$(mktempdir); register_cleanup "rm -rf '$work'"
mkdir -p "$work/home/etc"

"$CC" -o "$work/peer" "$root/tests/lib/tcp-silent-peer.c" 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; skip "tcp-silent-peer does not compile"; }

"$work/peer" 40 > "$work/port" &
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

timeout_s=5
ceiling_ms=1500		# ~30% of the timeout; the fix measures ~0, the bug ~7000

cat > "$work/home/etc/protocols.cfg" <<CFG
[stalled]
   expect "never arrives"
   options ssl
   port $port
CFG
printf '127.0.0.1\tpeer\t# stalled\n' > "$work/home/etc/hosts.cfg"

TIMEFORMAT='%U %S'
cpu=$( { time XYMONHOME="$work/home" "$XYMONNET" --no-update --noping \
	--dns=ip --timeout=$timeout_s >"$work/out.txt" 2>&1 ; } 2>&1 | tail -1 )
kill "$peer" 2>/dev/null || :

set -- $cpu
[ $# -eq 2 ] || fail "could not measure CPU (got '$cpu')"
ms=$(awk -v u="$1" -v s="$2" 'BEGIN { printf "%d", (u + s) * 1000 }')

[ "$ms" -le "$ceiling_ms" ] || fail \
	"the probe burned ${ms}ms of CPU while waiting ${timeout_s}s for a handshake
that never completes (ceiling ${ceiling_ms}ms). That is a busy loop on
SSL_connect(): it re-asks immediately instead of waiting for the direction
OpenSSL asked for, so a stalled TLS handshake occupies a core for its whole
duration (#452)."

pass "a stalled TLS handshake costs ${ms}ms of CPU, not a spinning core (#452)"
