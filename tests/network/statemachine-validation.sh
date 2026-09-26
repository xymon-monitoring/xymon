#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/network/statemachine-validation.sh
#
# The contradictions "at" and "fin" can be written into, refused when the file
# is read.
#
# Both keywords narrow what a step means, and both have a shape they cannot
# combine with. Left unchecked each fails later and elsewhere: an "at" beside an
# "until" matches at a byte offset in a reply the entry also says is lines, and
# a "fin" under TLS sends a bare FIN that the peer reads as a truncated session
# rather than an ended message. Neither looks like a configuration mistake by
# the time it surfaces -- one is a probe that never matches, the other a
# handshake error against a server that is fine.
#
# So each is refused where it is written, and each refusal is checked here by
# its message. THE LAST ROW IS THE CONTROL: the file this tree ships must
# trigger none of them. A validator that cries wolf on the shipped
# configuration is one every reader learns to ignore.
#
# LAYER: the parser. No sockets, no peers -- xymonnet is run only far enough to
# read the file.

set -eu
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

root=$(find_root)
require_bin XYMONNET xymonnet/xymonnet

work=$(mktempdir); register_cleanup "rm -rf '$work'"
mkdir -p "$work/home/etc"
printf '127.0.0.1\tnobody\t# conn\n' > "$work/home/etc/hosts.cfg"

run_xymonnet() {
	XYMONHOME="$work/home" "$XYMONNET" --no-update --noping --dns=ip \
		--timeout=2 2>&1 || :
}

# --- "at" together with "until" ----------------------------------------------
# A terminator ends a reply made of lines; an offset indexes into one that is
# not lines at all. Taking both would have to pick which, silently.
cat > "$work/home/etc/protocols2.cfg" <<'CFG'
[atanduntil]
   port 9
   start s

   state s
      expect "x" at 4 until "y"
CFG
out=$(run_xymonnet)
grep -qi "expect takes 'at' or 'until', not both" <<<"$out" || fail \
"an expect with both 'at' and 'until' was accepted. One says the reply is bytes
and the other says it is lines, so the entry matches at an offset in a reply it
also claims to terminate by line -- and nothing in the file says which wins:
$out"

# --- an offset past the buffer the driver can hold ---------------------------
# An offset beyond the cap could never match, so it is a probe that waits out
# its clock on every poll and reports the server down.
cat > "$work/home/etc/protocols2.cfg" <<'CFG'
[atrange]
   port 9
   start s

   state s
      expect "x" at 99999
CFG
out=$(run_xymonnet)
grep -qiE "'at 99999' is outside 0\.\.[0-9]+" <<<"$out" || fail \
"an offset past the driver's buffer cap was accepted. It can never match, so
the entry times out against a healthy server and reports it down:
$out"

# --- anything but "fin" after a send -----------------------------------------
# This arm ignored trailing text until "fin" existed. A near miss would be
# dropped in silence and the entry would wait for a reply the peer cannot send,
# which is the failure "untill" used to have on the expect side.
cat > "$work/home/etc/protocols2.cfg" <<'CFG'
[sendjunk]
   port 9
   start s

   state s
      send "hello" finn
      expect "x"
CFG
out=$(run_xymonnet)
grep -qi "send takes only 'fin', not" <<<"$out" || fail \
"a misspelled 'fin' was ignored rather than refused. The write side is never
retired, so a protocol that answers only after end-of-file waits forever, and
the file gives no hint why:
$out"

# --- "fin" on an encrypted connection ----------------------------------------
# A bare FIN under TLS is a truncated session to the peer, not an ended
# message: it reads as an attack rather than a request.
cat > "$work/home/etc/protocols2.cfg" <<'CFG'
[finssl]
   port 9
   options ssl
   start s

   state s
      send "hello" fin
      expect "x"
CFG
out=$(run_xymonnet)
grep -qi "'fin' cannot be used on an encrypted connection" <<<"$out" || fail \
"'fin' was accepted on an encrypted connection. A bare FIN is a truncation to a
TLS peer rather than the end of a message, so the failure arrives as a handshake
error against a server that is working:
$out"

# --- THE CONTROL: the shipped files must trigger none of it ------------------
cp "$root/xymonnet/protocols.cfg"  "$work/home/etc/protocols.cfg"
cp "$root/xymonnet/protocols2.cfg" "$work/home/etc/protocols2.cfg"
out=$(run_xymonnet)
noise=$(grep -ciE "expect takes 'at' or 'until'|is outside 0\.\.|send takes only 'fin'|'fin' cannot be used" <<<"$out" || true)
[ "$noise" -eq 0 ] || fail \
"the configuration this tree ships triggers $noise of these refusals. A
validator that fires on the shipped file is one every reader learns to skip:
$(grep -iE "at' or 'until'|outside 0\.\.|only 'fin'|cannot be used on an" <<<"$out")"

pass "'at' with 'until', an out-of-range offset, junk after a send and 'fin' under TLS are each refused, and the shipped files trigger none of them"
