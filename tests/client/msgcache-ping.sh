#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/client/msgcache-ping.sh
#
# msgcache answers "ping" locally, and says two things: what it is, and how
# long ago a Xymon server last collected from it.
#
# Both matter because msgcache never connects to the server -- the server
# connects to it with "pullclient". "The server is up" is therefore not
# msgcache's to say, and a caller that read it as such could upgrade a client
# against a server that has been gone for a week, while maxage quietly
# discarded its messages. The age of the last pull is the honest signal.
#
# The other half is what a ping must NOT disturb: the config the server pushed
# is held in one global, and answering through it would hand the next client a
# version string where its configuration belongs.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin MSGCACHE client/msgcache
command -v nc > /dev/null 2>&1 || skip "no nc to speak to msgcache with"

work=$(mktempdir)

free_port() {
	local p tries=0
	while [ "$tries" -lt 50 ]; do
		p=$(( 20000 + (RANDOM % 20000) ))
		nc -z 127.0.0.1 "$p" 2>/dev/null || { printf '%s' "$p"; return 0; }
		tries=$((tries + 1))
	done
	return 1
}

PORT=$(free_port) || skip "no free port for msgcache"
"$MSGCACHE" --listen=127.0.0.1:"$PORT" --server=127.0.0.1 --no-daemon > "$work/log" 2>&1 &
MC=$!
register_cleanup "kill $MC 2>/dev/null || true"

i=0
while [ "$i" -lt 50 ]; do
	nc -z 127.0.0.1 "$PORT" 2>/dev/null && break
	kill -0 "$MC" 2>/dev/null || { cat "$work/log" >&2; fail "msgcache exited during startup"; }
	sleep 0.1
	i=$((i + 1))
done

say() { printf '%s\n' "$1" | nc -q1 127.0.0.1 "$PORT" 2>/dev/null; }

# --- before any server has collected -----------------------------------------
out=$(say 'ping')
assert_contains "msgcache" "$out" "the ping names msgcache, not the daemon it never talks to"
assert_contains "lastpull -1" "$out" "and reports no pull at all until a server has collected"

# --- a server collects, pushing a client config ------------------------------
say 'pullclient 1
CONFIG-FROM-SERVER' > /dev/null

out=$(say 'ping')
assert_not_contains "lastpull -1" "$out" "after a pull, the age must be a real one"
assert_contains "lastpull " "$out" "and still be reported"

# --- and the ping did not eat the config -------------------------------------
# The reply travels through its own buffer, not through the global holding the
# pushed config: sharing it would give this client a version string instead.
out=$(say 'client host.linux linux')
assert_contains "CONFIG-FROM-SERVER" "$out" \
	"a client must still get the config the server pushed, after a ping"

pass "msgcache answers ping with its own identity and the age of the last pull, without disturbing the pushed config"
