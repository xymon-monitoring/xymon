#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/xymond-acklist-long-entry.sh
#
# xymondboard's acklist gives every acknowledgement of a status, one line
# each, however long its words.
#
# The critical-systems view acknowledges with ackinfo, and xymond lists those
# acknowledgements in the board's acklist field, one line each:
# received:validuntil:level:ackedby:msg. acklist_string() formatted each line
# into a 512-byte buffer first: a longer one was cut, and lost the newline
# that ends it, so the next acknowledgement ran on in the same line. A reader
# then saw one acknowledgement where there were two, the second's end time
# lost with it.
#
# Here one status gets two acknowledgements, a short one, then one with 600
# bytes of words. xymond lists the newest first, so the long line comes
# before the short one: acklist must hold two lines, the long one with all
# its words, the short one whole.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
# shellcheck source=tests/lib/xymond-daemon.sh
. "$(dirname "$0")/../lib/xymond-daemon.sh"

require_bin XYMOND xymond/xymond
require_bin XYMONCLIENT common/xymon

work=$(mktempdir)
XYMOND_PID=""
register_cleanup 'stop_process "$XYMOND_PID"'

printf 'page test Test\n127.0.0.1 ack.example.com # conn\n' > "$work/hosts.cfg"
mkdir -p "$work/home/etc" "$work/home/tmp" "$work/home/www"
require_cfg XYMONSERVER_CFG xymond/etcfiles/xymonserver.cfg
sed -e 's|^XYMONHOME=.*|XYMONHOME="'"$work"'/home"|' \
    -e 's|^XYMONTMP=.*|XYMONTMP="'"$work"'/home/tmp"|' \
	"$XYMONSERVER_CFG" > "$work/xymonserver.cfg"

xymond_launch() {
	local port=$1; shift
	"$XYMOND" --no-daemon --listen="127.0.0.1:$port" \
		--hosts="$work/hosts.cfg" --env="$work/xymonserver.cfg" \
		--pidfile="$work/xymond.pid" --checkpoint-file="$work/chk.out" \
		"$@" \
		> "$work/xymond.log" 2>&1 &
	XYMOND_PID=$!
}

start_xymond

send() { "$XYMONCLIENT" "127.0.0.1:$PORT" "$1" || fail "xymond rejected: $1"; }

long=$(printf '%600s' '' | tr ' ' w)
send "status ack,example,com.conn red down"
send "ackinfo ack.example.com.conn
1
7200
bob
short words"
send "ackinfo ack.example.com.conn
1
3600
alice
$long"

# The board, polled until both acknowledgements are in: xymond takes them
# in turn.
list=
for _ in $(seq 1 50); do
	list=$("$XYMONCLIENT" "127.0.0.1:$PORT" "xymondboard host=ack.example.com test=conn fields=acklist")
	case $list in *bob*alice*|*alice*bob*) break ;; esac
	sleep 0.1
done
# xymondboard encodes a newline inside a field as \n.
lines=$(printf '%s' "$list" | sed 's/\\n/\
/g' | grep -c ':1:' || true)

assert_equal "2" "$lines" "acklist did not give two acknowledgements, one line each: ${list:0:300}"
assert_contains ":1:alice:$long" "$list" "the long acknowledgement lost words past 512 bytes"
assert_contains ":1:bob:short words" "$list" "the short acknowledgement, after the long one, is not whole"

pass "xymondboard's acklist gives every acknowledgement on its own line, whatever the length of its words"
