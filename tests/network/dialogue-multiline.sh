#!/usr/bin/env bash
#
# A multi-line reply, which is what real SMTP sends.
#
# An expect consumes one line. EHLO answers with several, so without a way
# to say where the reply ENDS the remaining lines stay in the buffer and
# the next expect compares against "250-PIPELINING" and fails. Both
# services here are identical apart from the "until" clause, so the test
# carries its own control: if the second one starts passing, the
# terminator has stopped doing anything and this test is no longer
# checking what it claims to.
#
# Both end with an expect on the 221, and that matters: with nothing after
# the multi-line reply the leftover lines have nothing to collide with and
# even the broken entry passes. That is exactly why the shipped [smtp]
# definition gets away without a terminator today.

set -eu
. "$(dirname "$0")/../lib/assert.sh"
root=$(find_root)

require_bin XYMONNET xymonnet/xymonnet
require_cc

work=$(mktempdir); register_cleanup "rm -rf '$work'"
mkdir -p "$work/home/etc"

"$CC" -o "$work/peer" "$root/tests/lib/dialogue-peer.c" -lssl -lcrypto 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; skip "dialogue-peer does not compile"; }

cat > "$work/ml.script" <<'EOS'
send "220 test.local ESMTP\r\n"
recv ehlo
send "250-test.local\r\n250-PIPELINING\r\n250 CHUNKING\r\n"
recv quit
send "221 bye\r\n"
EOS

start() {
	"$work/peer" "$work/ml.script" "$2" > "$1" &
	echo $! >> "$work/pids"
	local i=0
	while [ "$i" -lt 50 ]; do [ -s "$1" ] && break; sleep 0.1; i=$((i + 1)); done
	cat "$1"
}
: > "$work/pids"
p1=$(start "$work/p1" "$work/o1")
p2=$(start "$work/p2" "$work/o2")
register_cleanup "kill $(tr '\n' ' ' < "$work/pids") 2>/dev/null || :"
[ -n "$p1" ] && [ -n "$p2" ] || fail "a peer never named its port"

cat > "$work/home/etc/protocols.cfg" <<CFG
[mlok]
   expect "220"
   send "ehlo xymonnet\r\n"
   expect "250" until "250 "
   send "quit\r\n"
   expect "221"
   options banner
   port $p1

[mlbad]
   expect "220"
   send "ehlo xymonnet\r\n"
   expect "250"
   send "quit\r\n"
   expect "221"
   options banner
   port $p2
CFG
printf '127.0.0.1\twith\t# mlok\n127.0.0.1\twithout\t# mlbad\n' > "$work/home/etc/hosts.cfg"

XYMONHOME="$work/home" "$XYMONNET" --no-update --noping --checkresponse \
	--dns=ip --timeout=15 >"$work/out.txt" 2>&1 || :
sleep 1

grep -q 'Service mlok on with is OK' "$work/out.txt" || fail \
	'expect ... until "250 " did not consume the multi-line reply:
'"$(cat "$work/out.txt")"

grep -q 'Service mlbad on without is OK' "$work/out.txt" && fail \
	"the entry WITHOUT a terminator passed too, so the terminator is not what
made the difference and this test proves nothing. Either an expect has
started consuming whole replies by itself, or the peer stopped sending
more than one line."

grep -q 'got quit' "$work/o1" || fail \
	"the dialogue never reached QUIT: $(cat "$work/o1")"

# --- a final line that is the bare code, with no text after it ---------------
#
# "until" names the terminator as a literal, and "250 " carries a trailing
# space because a continuation is "250-" and the last line is "250 text". A
# server with nothing to add sends "250" and stops: RFC 5321 4.2 puts the
# space before TEXT, and there is no text. The literal then never matches,
# the expect waits for a line that has already been sent, and a healthy
# server is reported down when the clock runs out.
printf '%s\n' 'send "250-first\r\n250\r\n"' 'hold 6' > "$work/bare.script"
"$work/peer" "$work/bare.script" "$work/bare.obs" > "$work/bare.port" &
echo $! >> "$work/pids"
i=0; while [ "$i" -lt 50 ]; do [ -s "$work/bare.port" ] && break; sleep 0.1; i=$((i + 1)); done
bport=$(cat "$work/bare.port")
[ -n "$bport" ] || skip "the bare-code peer never named a port"

printf '[bare]\n   expect "250" until "250 "\n   options banner\n   port %s\n' \
	"$bport" > "$work/home/etc/protocols.cfg"
printf '127.0.0.1\tbarehost\t# bare\n' > "$work/home/etc/hosts.cfg"

bstart=$(date +%s)
bout=$(XYMONHOME="$work/home" "$XYMONNET" --no-update --noping --checkresponse \
	--dns=ip --timeout=8 2>&1 || :)
belapsed=$(( $(date +%s) - bstart ))
bcolour=$(grep -oE 'barehost\.bare (green|yellow|red|clear)' <<<"$bout" | awk '{print $2}' | head -1)

[ "$bcolour" = "green" ] || fail \
	"a reply whose last line is the bare code reported '$bcolour' after
${belapsed}s. \"250-first\" then \"250\" is a complete multi-line reply --
the second line ends it, and it carries no text for the space to precede:
$(grep -i barehost <<<"$bout" | head -3)"

# --- "until" applies to a command-first entry too ---------------------------
#
# Whether an entry is driven as a conversation is decided by its shape, and
# "until" was not part of that decision: an entry that sends first and has one
# expect looked like the classic single-shot pair, took the legacy path, and
# the terminator was silently ignored. The old prefix match is then satisfied
# by the FIRST line of a multi-line reply, so a server whose reply ends in a
# failure code reports up. Shipped entries escape it only because they start
# with an expect.
printf '%s\n' 'recvany' 'send "250-first\r\n500 broken\r\n"' 'hold 6' > "$work/cf.script"
"$work/peer" "$work/cf.script" "$work/cf.obs" > "$work/cf.port" &
echo $! >> "$work/pids"
i=0; while [ "$i" -lt 50 ]; do [ -s "$work/cf.port" ] && break; sleep 0.1; i=$((i + 1)); done
cfport=$(cat "$work/cf.port")
[ -n "$cfport" ] || skip "the command-first peer never named a port"

printf '[cmdfirst]\n   send "probe\\r\\n"\n   expect "250" until "250 "\n   port %s\n' \
	"$cfport" > "$work/home/etc/protocols.cfg"
printf '127.0.0.1\tcfhost\t# cmdfirst\n' > "$work/home/etc/hosts.cfg"

cfout=$(XYMONHOME="$work/home" "$XYMONNET" --no-update --noping --checkresponse \
	--dns=ip --timeout=8 2>&1 || :)
cfcolour=$(grep -oE 'cfhost\.cmdfirst (green|yellow|red|clear)' <<<"$cfout" | awk '{print $2}' | head -1)

[ "$cfcolour" != "green" ] || fail \
	"a reply ending in \"500 broken\" reported the service UP. The entry sends
before it expects, so it was classified as the old single-shot pair and the
\"until\" was dropped -- leaving a prefix match that the first line of the
reply satisfies:
$(grep -i cmdfirst <<<"$cfout" | head -3)"

pass "a multi-line reply is consumed through its terminator, and is not without one"
