#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/xymond-truncated-request.sh
#
# Half a status message is not a status message.
#
# A request to xymond has no terminator -- lib/sendmsg.c writes the message text
# and nothing else -- so the daemon takes the message to be finished when the
# read stops. That test used to be "n <= 0", which is a clean end of file AND a
# connection that broke, so a sender that died mid-message had its fragment
# handled as though it were whole: a colour set from a line cut in the middle,
# with nothing recording that anything was lost.
#
# The two cases are told apart now. This drives both against a real xymond,
# because it is the daemon's own framing rule that is under test.
#
#   complete   written, then a clean half-close -- must land
#   truncated  written, then RST mid-message    -- must NOT land
#
# THE FIRST ROW IS THE CONTROL. Discarding a fragment is easy to get right by
# discarding everything, and a suite that only checked the fragment would not
# notice. A sender closing with SO_LINGER 0 has its partial message accepted
# today, so this is exactly the behaviour that changes and the row that proves
# nothing else changed with it.
#
# LAYER: the daemon's receive loop, over a real socket.

set -eu
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
# shellcheck source=tests/lib/xymond-daemon.sh
. "$(dirname "$0")/../lib/xymond-daemon.sh"

root=$(find_root)
require_bin XYMOND xymond/xymond
require_bin XYMONCLIENT common/xymon
require_shm_segments "$(grep -c 'setup_channel(C_[A-Z_]*, CHAN_MASTER)' "$root/xymond/xymond.c")"
require_cfg XYMONSERVER_CFG xymond/etcfiles/xymonserver.cfg
require_c_buildenv "$root"

work=$(mktempdir); register_cleanup "rm -rf '$work'"
mkdir -p "$work/home/etc" "$work/home/tmp" "$work/home/www"
printf 'page test Test\n127.0.0.1 testhost.example.com # conn\n' > "$work/hosts.cfg"

sed -e 's|^XYMONHOME=.*|XYMONHOME="'"$work"'/home"|' \
    -e 's|^XYMONTMP=.*|XYMONTMP="'"$work"'/home/tmp"|' \
	"$XYMONSERVER_CFG" > "$work/xymonserver.cfg"

xymond_launch() {
	local port=$1; shift
	"$XYMOND" --no-daemon --listen="127.0.0.1:$port" \
		--hosts="$work/hosts.cfg" --env="$work/xymonserver.cfg" \
		--pidfile="$work/xymond.pid" "$@" \
		> "$work/xymond.log" 2>&1 &
	XYMOND_PID=$!
}

start_xymond
register_cleanup "kill '$XYMOND_PID' 2>/dev/null || :"

# One sender, two ways of ending the connection: a clean half-close, or RST
# from SO_LINGER 0. The bytes are identical; only the read result differs.
"$CC" -o "$work/sender" "$root/tests/lib/message-sender.c" 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; fail "the message sender does not compile"; }
send_msg() { "$work/sender" 127.0.0.1 "$PORT" "$1" "$2"; }

board() {	# testname -> the colour xymond holds for it, or nothing
	"$XYMONCLIENT" "127.0.0.1:$PORT" "xymondboard fields=testname,color" 2>/dev/null \
		| awk -F'|' -v t="$1" '$1 == t { print $2 }'
}

# --- the control: a complete message, ended cleanly ---------------------------
send_msg clean 'status testhost,example,com.whole green everything arrived'
sleep 0.5

[ "$(board whole)" = green ] || fail \
"a complete status message, ended with a clean half-close, did not reach the
daemon (board says '$(board whole)'). Discarding fragments must not mean
discarding messages:
$(tail -5 "$work/xymond.log")"

# --- the fragment: cut off mid-message ---------------------------------------
send_msg reset 'status testhost,example,com.frag green the sender died right he'
sleep 0.5

[ -z "$(board frag)" ] || fail \
"a status message whose connection was reset mid-send was accepted as complete
-- the board reports '$(board frag)' for it. Nothing in the bytes says the
message was cut: the sender stopping IS the terminator, so a broken connection
and a finished message look identical unless the read result is checked:
$(tail -5 "$work/xymond.log")"

# It must be dropped LOUDLY. A fragment thrown away in silence is a status that
# vanishes with no way to find out why.
grep -q 'Truncated message' "$work/xymond.log" || fail \
"the fragment was dropped without a word in the log. Someone chasing a status
that never appeared has nothing to find:
$(tail -10 "$work/xymond.log")"

# --- and a connection that says nothing at all -------------------------------
send_msg clean ''
sleep 0.3
kill -0 "$XYMOND_PID" 2>/dev/null || fail \
"xymond did not survive a connection that opened and closed without sending
anything -- which is what every port scan does:
$(tail -5 "$work/xymond.log")"

# The daemon is still serving after all three.
[ "$(board whole)" = green ] || fail \
"the daemon stopped answering after the fragment and the empty connection; the
status it had accepted is gone:
$(tail -5 "$work/xymond.log")"

pass "a reset mid-message is dropped and logged, a clean half-close still lands, and an empty connection is neither"
