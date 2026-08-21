#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/client/msgcache-ping.sh
#
# msgcache answers "ping" with what it is and how long ago a server last
# collected from it -- it never calls the server, so that age is the only
# honest signal it has.
#
# Pinned: the reply names msgcache and says -1 until a pull happens; a
# lookalike ("pingpong") is forwarded, not answered; queued data alone does
# not advance the age; the pushed config survives a ping.
#
# Driven with the in-tree xymon CLI, not nc: nc's half-close flag differs
# per variant and is missing on the BSDs.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin MSGCACHE client/msgcache

# Server builds produce common/xymon; client-only builds ship the same CLI
# as client/xymon. Probe the server path first, fall back to the client one;
# an explicit $XYMON (CMake out-of-source, autopkgtest) is used verbatim.
default="common/xymon"
if [ -z "${XYMON:-}" ] && [ ! -x "$(find_root)/$default" ] \
		&& [ -x "$(find_root)/client/xymon" ]; then
	default="client/xymon"
fi
require_bin XYMON "$default"

work=$(mktempdir)

# Probe with bash's own /dev/tcp so the port choice needs no external tool.
# The connect succeeding means the port is taken.
port_taken() {
	( exec 3<> "/dev/tcp/127.0.0.1/$1" ) 2>/dev/null
}

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

# Wait for msgcache to answer, not merely for the port to open: the first
# successful ping doubles as the startup probe. Fail loud on timeout rather
# than falling through into a misleading protocol assertion.
out=
i=0
while [ "$i" -lt 50 ]; do
	if out=$(say 'ping' 2>/dev/null) && [ -n "$out" ]; then break; fi
	kill -0 "$MC" 2>/dev/null || { cat "$work/log" >&2; fail "msgcache exited during startup"; }
	sleep 0.1
	i=$((i + 1))
done
[ -n "$out" ] || { cat "$work/log" >&2; fail "msgcache never answered a ping within 5s"; }

# --- before any server has collected -----------------------------------------
assert_contains "msgcache" "$out" "the ping names msgcache, not the daemon it never talks to"
assert_contains "lastpull -1" "$out" "and reports no pull at all until a server has collected"

# --- a ping lookalike is data, not a ping -------------------------------------
# "pingpong ..." shares the first four bytes; it must be queued for the server,
# not answered (and eaten) as a ping.
out=$(say 'pingpong is client data')
[ -z "$out" ] || fail "a message merely starting with ping must get no ping reply, got: $out"

out=$(say 'ping')
assert_contains "lastpull -1" "$out" "queued client data is not a pull; the age must still say never"

# --- a server collects, pushing a client config ------------------------------
out=$(say 'pullclient 1
CONFIG-FROM-SERVER')
assert_contains "pingpong is client data" "$out" \
	"the pull must deliver the ping lookalike the queue was holding"

out=$(say 'ping')
assert_not_contains "lastpull -1" "$out" "after a delivered pull, the age must be a real one"
assert_match 'lastpull [0-9]+' "$out" "and a non-negative number, not a sentinel or garbage"

# --- and the ping did not eat the config -------------------------------------
# The reply travels through its own buffer, not through the global holding the
# pushed config: sharing it would give this client a version string instead.
out=$(say 'client host.linux linux')
assert_contains "CONFIG-FROM-SERVER" "$out" \
	"a client must still get the config the server pushed, after a ping"

pass "msgcache answers exactly ping with its identity and the age of the last delivered pull, forwarding lookalikes and preserving the pushed config"
