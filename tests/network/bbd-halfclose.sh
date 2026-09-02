#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/network/bbd-halfclose.sh
#
# Xymon checking Xymon, against a real xymond.
#
# A request to xymond has no terminator. The daemon reads one until the read
# returns end of file and only then answers -- xymond.c, "End of input data on
# this connection". So a probe that sends "ping" and waits is never answered:
# from the daemon's side the request has not finished arriving. That is why
# [bbd] was connect-only for years, with a commented-out send/expect pair that
# could not work, and why a wedged xymond still accepting connections reported
# exactly like a healthy one.
#
# "fin" retires the write direction once the send is out. The daemon sees the
# end of the request, answers "xymond <version>", and the read side is still
# open to receive it.
#
# THE CONTROL IS THE SECOND ROW: the same entry, same live daemon, with "fin"
# removed. It must go red, and it must do so by waiting -- that is the failure
# the modifier exists to fix. Without that row the first row would only show
# that xymond answers pings, not that anything about the half-close mattered.
#
# The server here is the real xymond built from this tree, not a peer imitating
# one: the behaviour under test is the daemon's own framing rule, so there is
# nothing to imitate.
#
# LAYER: the whole path -- protocols2.cfg parsed, the driver writing and
# half-closing, a live xymond answering, and the colour that is reported.

set -eu
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
# shellcheck source=tests/lib/xymond-daemon.sh
. "$(dirname "$0")/../lib/xymond-daemon.sh"

root=$(find_root)
require_bin XYMONNET xymonnet/xymonnet
require_bin XYMOND xymond/xymond
require_bin XYMONCLIENT common/xymon
require_shm_segments "$(grep -c 'setup_channel(C_[A-Z_]*, CHAN_MASTER)' "$root/xymond/xymond.c")"
require_cfg XYMONSERVER_CFG xymond/etcfiles/xymonserver.cfg

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

# Both entries are the shipped [bbd] conversation. They differ in one word.
{
	sed -n '/^\[bbd|bb\]/,/^\[/p' "$root/xymonnet/protocols2.cfg" \
		| sed '$d' | sed "s/^   port .*/   port $PORT/; s/^\[bbd|bb\]/[bbdfin]/"
	printf '\n'
	sed -n '/^\[bbd|bb\]/,/^\[/p' "$root/xymonnet/protocols2.cfg" \
		| sed '$d' | sed "s/^   port .*/   port $PORT/; s/^\[bbd|bb\]/[bbdnofin]/; s/ fin$//"
} > "$work/home/etc/protocols2.cfg"

# A FAILURE, not a skip: this test exists to protect that line, and a skip in
# the summary reads like nothing is wrong. If the entry is deliberately
# restructured, this message is where to come and say so.
grep -q 'send "ping" fin' "$work/home/etc/protocols2.cfg" || fail \
"the shipped [bbd] entry no longer sends a ping with 'fin'. Without the
half-close xymond never sees the end of the request and never answers, so the
entry is back to being a connect-only check:
$(sed -n '/^\[bbdfin\]/,/^$/p' "$work/home/etc/protocols2.cfg")"
grep -q 'send "ping"$' "$work/home/etc/protocols2.cfg" || fail \
	"the control entry still has 'fin' -- it must be the same conversation WITHOUT it,
or the row below proves nothing:
$(cat "$work/home/etc/protocols2.cfg")"

printf '127.0.0.1\twithfin\t# bbdfin\n127.0.0.1\tnofin\t# bbdnofin\n' \
	> "$work/home/etc/hosts.cfg"

# A short timeout: the control row is expected to wait for a reply that never
# comes, and the test should not sit through the default budget to learn it.
XYMONHOME="$work/home" "$XYMONNET" --no-update --noping --checkresponse=red \
	--dns=ip --timeout=5 >"$work/out.txt" 2>&1 || :

colour_of() { grep -oE "status\+[0-9]+ $1 (green|yellow|red|clear)" "$work/out.txt" | awk '{print $3}' | head -1; }

kill -0 "$XYMOND_PID" 2>/dev/null || fail \
"xymond died during the run, so neither colour below means anything:
$(tail -5 "$work/xymond.log")"

[ "$(colour_of 'withfin\.bbdfin')" = green ] || fail \
"the shipped [bbd] entry did not report a live xymond as up (got
'$(colour_of 'withfin\.bbdfin')'). It sends \"ping\" and half-closes, which is
the only way this daemon learns a request has ended:
$(grep -i bbdfin "$work/out.txt" | head -3)"

# THE ROW THAT JUSTIFIES THE FEATURE.
[ "$(colour_of 'nofin\.bbdnofin')" = red ] || fail \
"the same entry WITHOUT 'fin' reported '$(colour_of 'nofin\.bbdnofin')' against
the same live xymond. It should have waited for a reply that never comes: the
daemon does not answer until the request ends, and nothing else ends it. If
this row is green, the half-close is not what made the row above work:
$(grep -i bbdnofin "$work/out.txt" | head -3)"

# ...and it must fail by WAITING. A connection refused would also be red and
# would prove nothing about framing.
grep -iE 'bbdnofin' "$work/out.txt" | grep -qiE 'timeout|timed out|no response|Unexpected service response' || fail \
"the control went red for some reason other than waiting for a reply, so it is
not reproducing the missing half-close:
$(grep -i bbdnofin "$work/out.txt" | head -5)"

pass "xymond answers a ping only once the probe stops writing: 'fin' makes [bbd] a real check"
