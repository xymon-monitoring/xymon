#!/usr/bin/env bash
# An entry with no steps is a connect check, and which entries those are is
# pinned here.
#
# Nothing is sent and nothing is matched, so the port opening is the whole
# check -- which is right for protocols this grammar cannot express, and is
# also what a half-written entry looks like. The file cannot tell those apart
# on its own, so the list lives here: adding a stepless entry means changing
# this test, deliberately, in the same commit.
#
# This replaces an "options connect-only" keyword that declared the same
# thing. It had no effect on the probe at all -- the flag never reached
# contest.c -- and the only rule it carried was that it could not appear
# beside a step, which is a contradiction that existed solely because the
# keyword did.

set -eu
. "$(dirname "$0")/../lib/assert.sh"
root=$(find_root)

require_bin XYMONNET xymonnet/xymonnet
require_cc

work=$(mktempdir); register_cleanup "rm -rf '$work'"
mkdir -p "$work/home/etc"

# Deliberately stepless, first alias of each header. All of them mean the same
# thing: this grammar cannot ask the protocol anything it could judge the
# answer to. Five wait for a length-prefixed binary request, and bbd answers
# only after the client half-closes the write side.
#
# Three near misses are NOT here. telnet and telnets have no command to send
# either, but they do have a greeting worth waiting for, so they ask for one
# with "start iac" and an empty expect -- which is what makes a telnetd that
# opens the port and never speaks report down instead of up. And ldaps checks
# exactly what ldap does, plus a TLS handshake, which is a step: "start tls"
# and nothing after it.
expected="bbd ldap lpd netbios-ssn qmqp qmtp"

actual=$(awk '
	/^\[/ {
		if (name != "" && steps == 0) print name
		name = $0
		sub(/^\[/, "", name); sub(/\|.*/, "", name); sub(/\].*/, "", name)
		steps = 0
		next
	}
	/^[[:space:]]*(send|expect|start)[[:space:]]/ { steps++ }
	END { if (name != "" && steps == 0) print name }
' "$root/xymonnet/protocols.cfg" | sort | tr '\n' ' ' | sed 's/ $//')

[ "$actual" = "$expected" ] || fail \
	"the set of entries with no steps changed.
  expected: $expected
  actual:   $actual
An entry with no steps sends nothing and matches nothing, so it reports up on
any host where the port is open. That is correct for a protocol this grammar
cannot speak, and indistinguishable from an entry somebody started and did not
finish -- so the list is pinned. If the change is intended, say so here."

# And the behaviour itself: no steps means the port opening is the check, so a
# server that opens and stays silent is up, without waiting out a timeout.
"$CC" -o "$work/peer" "$root/tests/lib/dialogue-peer.c" -lssl -lcrypto 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; skip "dialogue-peer does not compile"; }

printf 'hold 6\n' > "$work/s"
"$work/peer" "$work/s" "$work/obs" > "$work/port" &
echo $! > "$work/pid"
register_cleanup "kill \$(cat '$work/pid') 2>/dev/null || :"
i=0; while [ "$i" -lt 50 ]; do [ -s "$work/port" ] && break; sleep 0.1; i=$((i + 1)); done
port=$(cat "$work/port")
[ -n "$port" ] || fail "the peer never named its port"

printf '[quiet]\n   port %s\n' "$port" > "$work/home/etc/protocols.cfg"
printf '127.0.0.1\tqh\t# quiet\n' > "$work/home/etc/hosts.cfg"

XYMONHOME="$work/home" "$XYMONNET" --no-update --noping --checkresponse \
	--dns=ip --timeout=10 >"$work/out" 2>&1 || :

colour=$(grep -aoE 'qh\.quiet (green|yellow|red|clear)' "$work/out" | awk '{print $2}' | head -1)
[ "$colour" = "green" ] || fail \
	"an entry with no steps reported $colour against a server that opened the
port and said nothing. With nothing to send and nothing to match there is
nothing to fail on -- reading and requiring bytes is what 'expect \"\"' is for:
$(grep -a quiet "$work/out" | head -3)"

pass "an entry with no steps is a connect check, and the stepless entries are the expected $(printf '%s' "$expected" | wc -w | tr -d ' ')"
