#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# A server that speaks when it was not spoken to.
#
# IMAP sends untagged "* ..." lines whenever it likes -- an EXISTS, an
# EXPUNGE, a BYE warning -- and NNTP and XMPP have their own. A probe
# waiting for a tagged reply reads one of those, does not match it, and
# fails at once: correct by the rules of the grammar, wrong about the
# server, which was behaving exactly as its RFC says.
#
# "ignore PREFIX" says such a message is not an answer. It is consumed and
# the wait continues.
#
# THE CONTROL is [noisyplain]: the same peer, the same conversation, without
# the ignore line. It must go red -- if both pass, the ignore is doing
# nothing and this suite would pass with the feature removed.
#
# The second control is [quiet]: a peer that sends no untagged lines at all,
# against an entry that ignores them. Ignoring must not swallow the answer
# when there is no noise to skip.
#
# [imapreal] is the shape a real IMAP server has, and the one this feature
# could not express until 'ignore' became positional: the greeting IS an
# untagged line, so the SAME "* " prefix must be taken as the answer there
# and skipped as noise three lines later. It is its own control -- green
# requires both, and either half alone turns it red.

set -eu
. "$(dirname "$0")/../lib/assert.sh"
root=$(find_root)

require_bin XYMONNET xymonnet/xymonnet
require_cc

work=$(mktempdir); register_cleanup "rm -rf '$work'"
mkdir -p "$work/home/etc"

"$CC" -o "$work/peer" "$root/tests/lib/dialogue-peer.c" -lssl -lcrypto 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; skip "dialogue-peer does not compile"; }

# Untagged chatter before the greeting, and again before the tagged reply.
printf '%s\n' 'send * OK [CAPABILITY IMAP4rev1] ready\r\n' \
	      'send A01 OK ready\r\n' \
	      'recvany' \
	      'send * 3 EXISTS\r\n' \
	      'send * 1 RECENT\r\n' \
	      'send A02 OK CAPABILITY completed\r\n' \
	      'hold 20'                                    > "$work/noisy.script"
printf '%s\n' 'send A01 OK ready\r\n' \
	      'recvany' \
	      'send A02 OK CAPABILITY completed\r\n' \
	      'hold 20'                                    > "$work/quiet.script"
# A real IMAP server: the greeting is itself an untagged line.
printf '%s\n' 'send * OK [CAPABILITY IMAP4rev1] ready\r\n' \
	      'recvany' \
	      'send * CAPABILITY IMAP4rev1 IDLE NAMESPACE\r\n' \
	      'send A02 OK CAPABILITY completed\r\n' \
	      'hold 20'                                    > "$work/imap.script"

: > "$work/pids"
start() {	# script portfile obsfile
	"$work/peer" "$1" "$3" > "$2" &
	echo $! >> "$work/pids"
	local i=0
	while [ "$i" -lt 50 ]; do [ -s "$2" ] && break; sleep 0.1; i=$((i + 1)); done
	cat "$2"
}
pnoisy=$(start "$work/noisy.script" "$work/p1" "$work/o1")
pplain=$(start "$work/noisy.script" "$work/p2" "$work/o2")
pquiet=$(start "$work/quiet.script" "$work/p3" "$work/o3")
pimap=$(start  "$work/imap.script"  "$work/p4" "$work/o4")
register_cleanup "kill $(tr '\n' ' ' < "$work/pids") 2>/dev/null || :"
[ -n "$pnoisy" ] && [ -n "$pplain" ] && [ -n "$pquiet" ] && [ -n "$pimap" ] \
	|| fail "a peer never named its port"

cat > "$work/home/etc/protocols.cfg" <<CFG
[noisyok]
   options banner
   ignore "* "
   port $pnoisy

   state greeting
      timeout(10)                 -> fail
      expect "A01 OK"             -> ask

   state ask
      send "A02 CAPABILITY\r\n"
      timeout(10)                 -> fail
      expect "A02 OK"             -> success

[noisyplain]
   options banner
   port $pplain

   state greeting
      timeout(5)                  -> fail
      expect "A01 OK"             -> ask

   state ask
      send "A02 CAPABILITY\r\n"
      timeout(5)                  -> fail
      expect "A02 OK"             -> success

[quiet]
   options banner
   ignore "* "
   port $pquiet

   state greeting
      timeout(10)                 -> fail
      expect "A01 OK"             -> ask

   state ask
      send "A02 CAPABILITY\r\n"
      timeout(10)                 -> fail
      expect "A02 OK"             -> success

[imapreal]
   options banner
   ignore "* "
   port $pimap

   state greeting
      timeout(10)                 -> fail
      expect "* OK"               -> ask

   state ask
      send "A02 CAPABILITY\r\n"
      timeout(10)                 -> fail
      expect "A02 OK"             -> success
CFG
{ printf '127.0.0.1\tn\t# noisyok\n127.0.0.1\tp\t# noisyplain\n127.0.0.1\tq\t# quiet\n127.0.0.1\ti\t# imapreal\n'; } \
	> "$work/home/etc/hosts.cfg"

XYMONHOME="$work/home" "$XYMONNET" --no-update --noping --checkresponse=red \
	--dns=ip --timeout=30 >"$work/out.txt" 2>&1 || :

colour_of() { grep -oE "status\+[0-9]+ $1 (green|yellow|red|clear)" "$work/out.txt" | awk '{print $3}' | head -1; }

[ "$(colour_of n.noisyok)" = green ] || fail \
	"a server sending untagged lines before and during the exchange was not
followed. Those lines are not answers, and a probe that reads one as the
answer reports a working server as broken:
$(head -25 "$work/out.txt")"

# THE CONTROL: without 'ignore' the same peer must fail, or the line is inert
[ "$(colour_of p.noisyplain)" = green ] && fail \
	"the same peer passed without an 'ignore' line, so untagged messages are
being skipped by something else and this suite proves nothing:
$(grep -i noisyplain "$work/out.txt" | head -4)"

# THE SECOND CONTROL: ignoring must not eat the answer when nothing is noisy
[ "$(colour_of q.quiet)" = green ] || fail \
	"a peer that sent no untagged lines failed against an entry that ignores
them, so 'ignore' is consuming replies it was never meant to touch:
$(grep -i quiet "$work/out.txt" | head -4)"

# THE SHAPE THAT SETTLES IT: one prefix, both roles, decided by position.
# Green needs "* OK ..." taken as the greeting AND "* CAPABILITY ..." skipped
# three lines later. Taking both, or skipping both, is red.
[ "$(colour_of i.imapreal)" = green ] || fail \
	"a real IMAP exchange failed. Its greeting is an untagged '* OK' line and
its later '* CAPABILITY' line is noise, so the same prefix has to be the
answer in one state and skipped in another. Deciding that by the text
instead of by position is what made IMAP inexpressible:
$(grep -i imapreal "$work/out.txt" | head -6)
peer saw:
$(cat "$work/o4")"

pass "an unprompted message is skipped and the wait continues, and the prefix that also greets is still an answer"
