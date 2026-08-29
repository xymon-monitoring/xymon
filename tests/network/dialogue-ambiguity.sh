#!/usr/bin/env bash
#
# Two alternatives that overlap must be refused, not resolved.
#
# Consecutive expects are alternatives and the first match wins, but an
# alternative cannot be decided until enough of the reply has arrived.
# A shorter pattern that already fits therefore used to beat a longer one
# still in flight -- so the SAME config against the SAME bytes gave
# opposite verdicts depending only on whether the server wrote its reply
# in one call or two. Measured, before this was fixed: red for one write,
# green for two.
#
# That was first fixed by deferring the decision until every pattern in
# the group was decidable. This is the stronger fix: the definition is
# refused when the file is read, so the situation cannot arise. Two
# patterns can both match one reply exactly when one is a prefix of the
# other, so the check is complete rather than a guess, and refusing costs
# nothing a deferral was buying.
#
# The refused service must still report -- and report RED. Dropping it
# would remove the column from the display, which looks like a service
# nobody configured rather than one whose definition is wrong.
#
# The message is asserted too, not just the refusal. What the author
# wanted is reasonable, so an error that only says "these overlap" leaves
# them stuck; it has to name the fix.

set -eu
. "$(dirname "$0")/../lib/assert.sh"
root=$(find_root)

require_bin XYMONNET xymonnet/xymonnet
require_cc

work=$(mktempdir); register_cleanup "rm -rf '$work'"
mkdir -p "$work/home/etc"

"$CC" -o "$work/peer" "$root/tests/lib/dialogue-peer.c" -lssl -lcrypto 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; skip "dialogue-peer does not compile"; }

# The identical reply, written once and written in two pieces.
cat > "$work/one.script" <<'EOS'
send "220 ready\r\n"
recv x
send "250 OK\r\n"
EOS
cat > "$work/two.script" <<'EOS'
send "220 ready\r\n"
recv x
send "250"
send " OK\r\n"
EOS

: > "$work/pids"
start() {
	"$work/peer" "$1" "$3" > "$2" &
	echo $! >> "$work/pids"
	local i=0
	while [ "$i" -lt 50 ]; do [ -s "$2" ] && break; sleep 0.1; i=$((i + 1)); done
	cat "$2"
}
p1=$(start "$work/one.script" "$work/p1" "$work/o1")
p2=$(start "$work/two.script" "$work/p2" "$work/o2")
register_cleanup "kill $(tr '\n' ' ' < "$work/pids") 2>/dev/null || :"
[ -n "$p1" ] && [ -n "$p2" ] || fail "a peer never named its port"

# "250 OK" and "250" overlap: either could match "250 OK".
blk() {
cat <<CFG
[$1]
   expect "220"
   send "x\r\n"
   expect "250 OK"             -> fail
   expect "250"
   send "quit\r\n"
   options banner
   port $2
CFG
}
{ blk amb1 "$p1"; echo; blk amb2 "$p2"; } > "$work/home/etc/protocols.cfg"
printf '127.0.0.1\tonewrite\t# amb1\n127.0.0.1\tsplit\t# amb2\n' > "$work/home/etc/hosts.cfg"

out=$(XYMONHOME="$work/home" "$XYMONNET" --no-update --noping --checkresponse \
	--dns=ip --timeout=15 2>&1 || :)

grep -qi 'overlap' <<<"$out" || fail \
	"two alternatives where one is a prefix of the other were accepted without
comment. Which of them matches depends on how the server split its reply,
so the config's meaning is decided by the network:
$out"

grep -qi "expect ... as NAME" <<<"$out" || fail \
	"the overlap was reported without saying what to do about it. What the
author wanted is reasonable -- distinguishing two replies that share a
prefix -- so the error has to name the fix, not just the fault:
$out"

# THE POINT. A refused definition must not be able to report OK, however
# the server behaves, and must not vary with how the reply was split.
grep -q 'Service amb1 on onewrite is OK' <<<"$out" && fail \
	"a definition refused when the file was read still reported the service up:
$out"

grep -q 'Service amb2 on split is OK' <<<"$out" && fail \
	"a definition refused when the file was read still reported the service up
when the reply arrived in two writes:
$out"

# And it must still be reported at all -- refusing is not the same as
# dropping the test, which would look like a service nobody configured.
grep -q 'Service amb1 on onewrite is not OK' <<<"$out" || fail \
	"the refused service produced no status at all. It should report red with
the reason, not vanish from the display:
$out"

pass "overlapping alternatives are refused, the error names the fix, and the service reports red either way"
