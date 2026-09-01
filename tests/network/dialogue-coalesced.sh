#!/usr/bin/env bash
# Two replies in one packet, and the reply that carries the verdict.
#
# A reply is not a read. A server answering twice in quick succession lands
# both in one read, and the driver keeps the tail for the next step -- but
# waited for another read before looking at it. If the peer has said all it
# means to say, that wait ends at the timeout.
#
# IMAP's LOGOUT is answered twice: untagged "* BYE", then the tagged result.
# Checking only the first accepts a LOGOUT that failed, and reading the
# second is what creates the shipped file's first pair of adjacent expects.

set -eu
. "$(dirname "$0")/../lib/assert.sh"
root=$(find_root)

require_bin XYMONNET xymonnet/xymonnet

: "${CC:=cc}"
command -v "$CC" >/dev/null 2>&1 || skip "no C compiler"

work=$(mktempdir); register_cleanup "rm -rf '$work'"
mkdir -p "$work/home/etc"

"$CC" -o "$work/peer" "$root/tests/lib/dialogue-peer.c" -lssl -lcrypto 2>"$work/cc.log" || \
	skip "dialogue-peer does not compile against libssl"

: > "$work/peerpids"
register_cleanup "kill \$(tr '\n' ' ' < '$work/peerpids') 2>/dev/null || :"

# dialogue-peer serves one connection and exits, so every case gets its own.
new_peer() {
	"$work/peer" "$work/script.$1" "$work/observed.$1" > "$work/port.$1" &
	echo $! >> "$work/peerpids"
	i=0; while [ "$i" -lt 60 ]; do [ -s "$work/port.$1" ] && break; sleep 0.1; i=$((i + 1)); done
	cat "$work/port.$1"
}

probe() {
	XYMONHOME="$work/home" "$XYMONNET" --no-update --noping --checkresponse \
		--dns=ip --timeout=8 2>&1 || :
}

colour_of() {
	grep -oE "status\+[0-9]+ $1 (green|yellow|red|clear)" <<<"$2" | awk '{print $3}' | head -1
}

# --- 1. both replies arrive in one read, and both are consumed ----------
#
# One "send" is one write, so both lines reach the probe together. The
# second expect has its answer the moment the first matches.
printf '%s\n' 'send "220 one\r\n* two ready\r\n"' 'hold 6' > "$work/script.1"
port=$(new_peer 1)
[ -n "$port" ] || skip "the test peer never named a port"

printf '[coalesced]\n   expect "220"\n   expect "* two"\n   options banner\n   port %s\n' \
	"$port" > "$work/home/etc/protocols.cfg"
printf '127.0.0.1\thost1\t# coalesced\n' > "$work/home/etc/hosts.cfg"

start=$(date +%s)
out=$(probe)
elapsed=$(( $(date +%s) - start ))
colour=$(colour_of 'host1\.coalesced' "$out")

[ "$colour" = "green" ] || fail \
	"two replies arrived in one read and the second was never matched. Both
lines were already in the buffer when the first expect completed, so the
step that follows it had its answer -- the probe waited for a further read
instead and reported '$colour' after ${elapsed}s against a server that had
replied correctly:
$(grep -i coalesced <<<"$out" | head -3)"

# --- 1b. and a line that has not finished is not a reply either ---------
#
# "220" is matched by three bytes, but matching the pattern is not the reply
# having arrived: completing there consumes the unfinished line too, and the
# next expect starts mid-word. Segment boundaries fall where the network
# puts them.
printf '%s\n' 'send "220 hel"' 'hold 1' 'send "lo\r\n250 ok\r\n"' 'hold 6' > "$work/script.1b"
port=$(new_peer 1b)

printf '[partial]\n   expect "220"\n   expect "250"\n   options banner\n   port %s\n' \
	"$port" > "$work/home/etc/protocols.cfg"
printf '127.0.0.1\thost1b\t# partial\n' > "$work/home/etc/hosts.cfg"

out=$(probe)
colour=$(colour_of 'host1b\.partial' "$out")

[ "$colour" = "green" ] || fail \
	"an expect completed on a line that had not arrived yet. \"220\" matched
after three bytes, the rest of that line was consumed with it, and the next
expect began in the middle of the word it was cut through -- so a server that
simply sent its reply in two segments reported '$colour':
$(grep -i partial <<<"$out" | head -3)"

# --- 1c. but the last thing a server says need not end in a newline -----
#
# Waiting for the line to finish is right while another step follows. When
# nothing does, insisting on a terminator that may never come turns a reply
# the probe already matched into a timeout. A lone expect is driven here,
# not by the legacy path, so the rule reaches entries that never asked.
printf '%s\n' 'send "OK"' 'hold 6' > "$work/script.1c"
port=$(new_peer 1c)

printf '[lastline]\n   expect "OK"\n   port %s\n' "$port" > "$work/home/etc/protocols.cfg"
printf '127.0.0.1\thost1c\t# lastline\n' > "$work/home/etc/hosts.cfg"

start=$(date +%s)
out=$(probe)
elapsed=$(( $(date +%s) - start ))
colour=$(colour_of 'host1c\.lastline' "$out")

[ "$colour" = "green" ] || fail \
	"a server whose whole reply is \"OK\", with no newline after it, reported
'$colour' after ${elapsed}s. The pattern matched and nothing follows it, so
there is no next step to protect from a half-consumed line -- the probe
waited for a terminator the protocol never promised:
$(grep -i lastline <<<"$out" | head -3)"

# --- 1d. and bytes that arrived BEFORE a command cannot answer it -----------
#
# The tail kept for the next step is right while the steps only read. Across a
# send it is not: anything already in the buffer reached us before the command
# went out, so it cannot be the reply to it.
#
# This peer preloads a "250" with its greeting, then refuses the EHLO. If the
# preloaded line is allowed to answer the command, the probe reports a healthy
# server that just rejected it -- the injection the STARTTLS boundary already
# discards for, one step type over, and it needs no TLS at all.
printf '%s\n' 'send "220 mx\r\n250 preloaded\r\n"' 'recvany' \
	      'send "500 EHLO rejected\r\n"' 'hangup' > "$work/script.1d"
port=$(new_peer 1d)

printf '[stale]\n   expect "220" until "220 "\n   send "ehlo xymonnet\\r\\n"\n   expect "250" until "250 "\n   port %s\n' \
	"$port" > "$work/home/etc/protocols.cfg"
printf '127.0.0.1\thost1d\t# stale\n' > "$work/home/etc/hosts.cfg"

out=$(probe)
colour=$(colour_of 'host1d\.stale' "$out")

[ "$colour" != "green" ] || fail \
	"a reply that arrived before the command was sent was accepted as the
answer to it. The server refused the EHLO with 500; the 250 the probe matched
was preloaded with the greeting, so any peer can decide what our next command
appears to have returned:
$(grep -i stale <<<"$out" | head -3)"

# And the command must not have gone out. Noticing afterwards is too late: the
# next step of a custom entry may be an AUTH or anything else state-changing,
# handed to a peer that has already shown it is misbehaving.
i=0; while [ "$i" -lt 50 ] && ! grep -q '^done' "$work/observed.1d" 2>/dev/null; do
	sleep 0.1; i=$((i + 1))
done
! grep -q '^got ' "$work/observed.1d" || fail \
	"the probe sent its command and only then noticed the peer had preloaded a
reply. The stale bytes were there before the write, so the write should not
have happened:
$(cat "$work/observed.1d")"

# --- 2. IMAP: "* BYE" then a tagged failure is not a success ------------
#
# The peer sends both lines in one write, as a real server does. "* BYE" is
# present either way; the tagged line says the command failed.
printf '%s\n' 'send "* OK dovecot ready\r\n"' 'recv ABC123 LOGOUT' \
	      'send "* BYE Logging out\r\nABC123 NO logout failed\r\n"' 'hold 6' > "$work/script.2"
port=$(new_peer 2)

# Point the shipped imap entry at the peer. Written to a new file rather than
# edited in place: in-place editing is spelled differently on BSD and on GNU,
# and the suite runs on both.
sed "s/^   port 143\$/   port $port/" "$root/xymonnet/protocols.cfg" \
	> "$work/home/etc/protocols.cfg"
grep -q "^   port $port\$" "$work/home/etc/protocols.cfg" || \
	skip "could not point the shipped imap entry at the test peer"
printf '127.0.0.1\thost2\t# imap\n' > "$work/home/etc/hosts.cfg"

out=$(probe)
colour=$(colour_of 'host2\.imap' "$out")

[ "$colour" != "green" ] || fail \
	"an IMAP LOGOUT that the server refused reported the service UP. The
untagged '* BYE' is sent whether the command succeeded or not; the tagged
'ABC123 NO' is the result, and it is the line that was never read:
$(grep -i 'host2' <<<"$out" | head -3)
peer saw: $(cat "$work/observed.2")"

# --- 3. and the ordinary IMAP case still passes -------------------------
#
# A check that fails on a healthy server is worse than the gap it closes.
printf '%s\n' 'send "* OK dovecot ready\r\n"' 'recv ABC123 LOGOUT' \
	      'send "* BYE Logging out\r\nABC123 OK Logout completed\r\n"' 'hold 6' > "$work/script.3"
port=$(new_peer 3)

sed "s/^   port 143\$/   port $port/" "$root/xymonnet/protocols.cfg" \
	> "$work/home/etc/protocols.cfg"
printf '127.0.0.1\thost3\t# imap\n' > "$work/home/etc/hosts.cfg"

out=$(probe)
colour=$(colour_of 'host3\.imap' "$out")

[ "$colour" = "green" ] || fail \
	"a healthy IMAP server reported '$colour'. The server greeted, answered
LOGOUT with '* BYE' and a tagged OK, and closed -- which is the whole of a
successful check:
$(grep -i 'host3' <<<"$out" | head -3)
peer saw: $(cat "$work/observed.3")"

pass "replies coalesced into one read are all consumed, and IMAP reads the tagged result of its LOGOUT"
