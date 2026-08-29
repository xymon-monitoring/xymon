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
register_cleanup "kill $(tr '\n' ' ' < "$work/pids") 2>/dev/null || :"
[ -n "$pnoisy" ] && [ -n "$pplain" ] && [ -n "$pquiet" ] || fail "a peer never named its port"

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
CFG
{ printf '127.0.0.1\tn\t# noisyok\n127.0.0.1\tp\t# noisyplain\n127.0.0.1\tq\t# quiet\n'; } \
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

# --- what the file may not say -----------------------------------------------
cat > "$work/home/etc/protocols.cfg" <<CFG
[collide]
   ignore "A0"
   port 1

   state greeting
      timeout(5)                  -> fail
      expect "A01 OK"             -> success
CFG
printf '127.0.0.1\tc\t# collide\n' > "$work/home/etc/hosts.cfg"
XYMONHOME="$work/home" "$XYMONNET" --no-update --noping --checkresponse=red \
	--dns=ip --timeout=5 >"$work/bad.txt" 2>&1 || :

grep -qi "share a start" "$work/bad.txt" || fail \
	"an ignored prefix that also starts an expect was accepted. The reply
would be swallowed as noise or taken as the answer, and nothing in the file
says which:
$(head -10 "$work/bad.txt")"

pass "an unprompted message is skipped and the wait continues, and an ambiguous ignore is refused"
