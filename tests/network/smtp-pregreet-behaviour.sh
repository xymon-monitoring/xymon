#!/usr/bin/env bash
#
# The companion to smtp-no-pregreet.sh, which reads the source. This one
# runs the probe and asks what actually went over the wire.
#
# Both are needed. The source test is fast and needs no build, but it can
# only check that the code SAYS the right thing -- the ordering in
# protocols.cfg is half of it, the driver has to honour it.
#
# The peer answers 554 to anything arriving before its greeting, as Postfix
# does with smtpd_forbid_unauth_pipelining=yes (default since 3.9), so an
# unfixed probe fails here the way it fails in the field.
#
# The service definition is the one the tree SHIPS: protocols.cfg is copied
# and only the [smtp] port is redirected, so this covers the real entry
# rather than a convenient local one.

set -eu
. "$(dirname "$0")/../lib/assert.sh"
root=$(find_root)

require_bin XYMONNET xymonnet/xymonnet
require_cc

work=$(mktempdir); register_cleanup "rm -rf '$work'"
mkdir -p "$work/home/etc"

"$CC" -o "$work/peer" "$root/tests/lib/smtp-greet-peer.c" 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; skip "smtp-greet-peer does not compile"; }

"$work/peer" "$work/verdict.txt" > "$work/port" &
peer=$!
register_cleanup "kill $peer 2>/dev/null || :"

i=0
while [ "$i" -lt 50 ]; do
	[ -s "$work/port" ] && break
	kill -0 "$peer" 2>/dev/null || fail "the peer exited before naming its port"
	sleep 0.1
	i=$((i + 1))
done
port=$(cat "$work/port")
[ -n "$port" ] || fail "the peer never named a port"

# Copy the shipped file and redirect only the port inside [smtp].
awk -v port="$port" '
	/^[[:space:]]*\[/ { insmtp = ($0 ~ /^[[:space:]]*\[smtp\][[:space:]]*$/) }
	insmtp && /^[[:space:]]*port[[:space:]]/ { print "   port " port; next }
	{ print }
' "$root/xymonnet/protocols.cfg" > "$work/home/etc/protocols.cfg"

grep -q "port $port" "$work/home/etc/protocols.cfg" || fail "could not redirect the [smtp] port"
printf '127.0.0.1\tpeer\t# smtp\n' > "$work/home/etc/hosts.cfg"

XYMONHOME="$work/home" "$XYMONNET" --no-update --noping --checkresponse \
	--dns=ip --timeout=10 >"$work/out.txt" 2>&1 || :
wait "$peer" 2>/dev/null || :

[ -s "$work/verdict.txt" ] || fail "the peer recorded nothing -- the probe never connected"
verdict=$(cat "$work/verdict.txt")

grep -q '^polite$' <<<"$verdict" || fail \
	"the probe spoke before the server greeted it:
$verdict
Postfix >= 3.9 answers that with 554 and postscreen reads it as a spambot
signal (#450) -- the ordering in protocols.cfg is not being honoured."

if grep -q PIPELINED <<<"$verdict"; then
	fail "more than one command in a single write -- pipelining before the
server announced PIPELINING (#450):
$verdict"
fi

grep -q 'Service smtp on peer is OK' "$work/out.txt" || fail \
	"the probe did not report the service up:
$(cat "$work/out.txt")"

pass "the shipped [smtp] probe waits for the greeting and sends one command at a time (#450)"
