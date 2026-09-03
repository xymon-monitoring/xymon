#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/client/msgcache-failed-pull.sh
#
# A pull whose delivery is interrupted must not count. msgcache commits the
# "sent" bits for a batch only once the whole batch has been written, so:
#
#   - the messages are re-delivered on the next pull (not silently lost), and
#   - lastpull is not advanced by a collection that never landed.
#
# Regresses the earlier accounting, which marked messages sent while *building*
# the response: a dropped write then lost the batch, and the following empty
# retry refreshed lastpull -- reporting a healthy collection in exactly the
# failure the age is meant to expose.
#
# The interrupted pull is driven by a tiny compiled helper: it needs a half
# close (so msgcache replies) and a RST mid-read (so the write fails), neither
# of which bash's /dev/tcp can do.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin MSGCACHE client/msgcache
default="common/xymon"
if [ -z "${XYMON:-}" ] && [ ! -x "$(find_root)/$default" ] \
		&& [ -x "$(find_root)/client/xymon" ]; then
	default="client/xymon"
fi
require_bin XYMON "$default"
require_cc

work=$(mktempdir)

"$CC" -o "$work/interrupt" "$(dirname "$0")/msgcache-interrupt.c" 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; fail "interrupt helper does not compile"; }

# bash /dev/tcp probe: a successful connect means the port is taken.
port_taken() { ( exec 3<> "/dev/tcp/127.0.0.1/$1" ) 2>/dev/null; }
free_port() {
	local p tries=0
	while [ "$tries" -lt 50 ]; do
		p=$(( 20000 + (RANDOM % 20000) ))
		port_taken "$p" || { printf '%s' "$p"; return 0; }
		tries=$((tries + 1))
	done
	return 1
}

PORT=$(free_port) || skip "no free port for msgcache"
"$MSGCACHE" --listen=127.0.0.1:"$PORT" --server=127.0.0.1 --no-daemon > "$work/log" 2>&1 &
MC=$!
register_cleanup "kill $MC 2>/dev/null || true"

say() { "$XYMON" 127.0.0.1:"$PORT" "$1"; }

# The first successful ping doubles as the startup probe.
out=
i=0
while [ "$i" -lt 50 ]; do
	if out=$(say 'ping' 2>/dev/null) && [ -n "$out" ]; then break; fi
	kill -0 "$MC" 2>/dev/null || { cat "$work/log" >&2; fail "msgcache exited during startup"; }
	sleep 0.1
	i=$((i + 1))
done
[ -n "$out" ] || { cat "$work/log" >&2; fail "msgcache never answered a ping within 5s"; }

# Queue one large client message. It must exceed the kernel socket send buffer
# so the interrupted pull cannot complete the write in a single shot -- a small
# message the kernel buffers whole would look delivered even to a reader that
# never read it. Push it over a raw socket, not the CLI: a multi-megabyte argv
# would blow ARG_MAX on macOS.
marker="REDELIVER-ME-$$"
big=$(head -c 2097152 </dev/zero | tr '\0' X)
exec 4<> "/dev/tcp/127.0.0.1/$PORT"
printf '%s %s' "$marker" "$big" >&4
exec 4<&- 4>&-			# close -> EOF -> msgcache queues the message

# Queued data alone is not a pull.
out=$(say 'ping')
assert_contains "lastpull -1" "$out" "queued client data must not advance the pull age"

# Interrupted pull: request the batch, read a little, RST before it is all out.
# A helper that died before issuing its pull would leave every assertion below
# passing on a queue nobody ever pulled, so its exit status is part of the test.
"$work/interrupt" "$PORT" 1 || fail "the interrupt helper failed before it could interrupt a pull (exit $?)"

# A collection that never landed must not count as a pull.
out=$(say 'ping')
assert_contains "lastpull -1" "$out" \
	"a pull whose write was dropped mid-batch must not advance lastpull"

# ...and the batch must survive to be re-delivered on the next real pull.
out=$(say 'pullclient 1')
assert_contains "$marker" "$out" \
	"an interrupted pull must not lose its batch; the next pull re-delivers it"

# Now that a pull has actually landed, the age becomes real.
out=$(say 'ping')
assert_not_contains "lastpull -1" "$out" "after a delivered pull the age is a real one"
assert_match 'lastpull [0-9]+' "$out" "and a non-negative number, not the sentinel"

pass "an interrupted pull neither loses its batch nor advances lastpull; the next pull re-delivers and stamps it"
