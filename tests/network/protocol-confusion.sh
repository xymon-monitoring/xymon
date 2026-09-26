#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/network/protocol-confusion.sh
#
# Which entries can tell their own protocol from somebody else's.
#
# A service column claims more than "something is listening here" -- it claims
# the thing listening is an SMTP server. Ports outlive the services that opened
# them: hosts are decommissioned and reused, load balancers get repointed, a
# container lands on an address something else used to hold. An entry that
# accepts a reply from another protocol keeps reporting green through all of it.
#
# Every entry is probed against a minimal conforming server for every protocol,
# each generated from its own entry by mkpeer.awk. THE DIAGONAL IS THE CONTROL:
# an entry must accept its own peer, and if it does not then that peer does not
# speak its protocol and its whole column means nothing. The run is void rather
# than merely failing.
#
# The confusions that exist today are recorded in the fixture beside this test.
# It is compared in BOTH directions:
#
#   a pair that appears and is not listed    an entry got worse, or a new entry
#                                            was added without a real check
#   a pair that is listed and does not       the list is out of date, which is
#                                            good news that has to be written
#                                            down, or the next regression hides
#                                            behind it
#
# So the list cannot rot. It can only shrink, and shrinking it means editing the
# fixture deliberately.
#
# The peers are minimal, not complete: each says only what its own entry
# demands. A real SMTP server answers "221" to a quit and would also satisfy
# [ftp], which the generated one does not, so the recorded set is a LOWER bound
# on the confusions that exist.
#
# LAYER: every entry in the shipped configuration, end to end. ~90s: 28 peer
# protocols x 37 entries.

set -eu
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

root=$(find_root)
require_bin XYMONNET xymonnet/xymonnet
: "${CC:=cc}"
command -v "$CC" >/dev/null 2>&1 || skip "no C compiler: every cell below needs a peer"

expected="$root/tests/fixtures/protocol-confusions.txt"
[ -r "$expected" ] || fail "the recorded confusions are missing: $expected"

work=$(mktempdir); register_cleanup "rm -rf '$work'"
mkdir -p "$work/home/etc" "$work/peers"

"$CC" -o "$work/peer" "$root/tests/lib/dialogue-peer.c" -lssl -lcrypto 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; skip "dialogue-peer does not compile against libssl"; }

# One peer script per protocol, built from the entry that describes it.
awk -v dir="$work/peers" -f "$root/tests/lib/mkpeer.awk" \
	"$root/xymonnet/protocols2.cfg" > "$work/peernames" 2>"$work/peerskip"
peers=$(tr '\n' ' ' < "$work/peernames")
[ -n "$peers" ] || fail "mkpeer.awk produced no peers from protocols2.cfg"

# Every entry is probed, including the ones with no peer of their own.
entries=$(sed -n 's/^\[\([^]|]*\).*\]$/\1/p' "$root/xymonnet/protocols2.cfg" | LC_ALL=C sort -u)
[ -n "$entries" ] || fail "no service names could be read out of protocols2.cfg"

cp "$root/xymonnet/protocols.cfg"  "$work/home/etc/protocols.cfg"
cp "$root/xymonnet/protocols2.cfg" "$work/home/etc/protocols2.cfg"

: > "$work/observed"
: > "$work/diagonal"
for p in $peers; do
	: > "$work/pids"
	printf 'page test T\n' > "$work/home/etc/hosts.cfg"
	for e in $entries; do
		"$work/peer" "$work/peers/$p.peer" "$work/obs.$e" > "$work/port.$e" 2>/dev/null &
		echo $! >> "$work/pids"
	done

	i=0
	while [ "$i" -lt 60 ]; do
		ready=1
		for e in $entries; do [ -s "$work/port.$e" ] || ready=0; done
		[ "$ready" = 1 ] && break
		sleep 0.1; i=$((i + 1))
	done

	for e in $entries; do
		port=$(cat "$work/port.$e" 2>/dev/null || :)
		[ -n "$port" ] && printf '127.0.0.1\th%s\t# %s:%s\n' \
			"$(echo "$e" | tr -d '-')" "$e" "$port" >> "$work/home/etc/hosts.cfg"
	done

	XYMONHOME="$work/home" "$XYMONNET" --no-update --noping --checkresponse=red \
		--dns=ip --timeout=4 >"$work/out" 2>&1 || :

	for e in $entries; do
		h="h$(echo "$e" | tr -d '-')"
		c=$(grep -oE "status\+[0-9]+ $h\.[a-z0-9_|-]+ (green|yellow|red|clear)" "$work/out" \
			| awk '{print $3}' | head -1)
		if [ "$e" = "$p" ]; then
			printf '%s %s\n' "$e" "${c:-none}" >> "$work/diagonal"
		elif [ "$c" = green ]; then
			printf '%s %s\n' "$e" "$p" >> "$work/observed"
		fi
	done

	kill $(tr '\n' ' ' < "$work/pids") 2>/dev/null || :
done

# THE CONTROL. A peer its own entry rejects is not speaking that protocol, and
# every cell in its column is then meaningless -- so this is void, not failed.
broken=$(awk '$2 != "green" { printf "  %s (its own entry reported %s)\n", $1, $2 }' "$work/diagonal")
[ -z "$broken" ] || fail \
"these peers were not accepted by the entry they were generated from:
$broken
The peer does not speak the protocol it stands for, so nothing measured against
it means anything. mkpeer.awk and the entry have disagreed -- fix that before
reading any result below."

LC_ALL=C sort -u "$work/observed" > "$work/observed.sorted"
grep -vE '^[[:space:]]*(#|$)' "$expected" | LC_ALL=C sort -u > "$work/expected.sorted"

new=$(comm -23 "$work/observed.sorted" "$work/expected.sorted")
gone=$(comm -13 "$work/observed.sorted" "$work/expected.sorted")

[ -z "$new" ] || fail \
"these entries accepted a protocol that is not theirs, and are not recorded as
doing so:
$(echo "$new" | sed 's/^/  /')
Each line is 'entry peer': the entry reported the service UP against a server
speaking the other protocol. Either the entry lost a check it used to have, or
a new entry was added without one. If it genuinely cannot tell them apart, add
the line to tests/fixtures/protocol-confusions.txt with the reason."

[ -z "$gone" ] || fail \
"these confusions are recorded but no longer happen:
$(echo "$gone" | sed 's/^/  /')
That is good news, and the fixture is out of date. Remove those lines from
tests/fixtures/protocol-confusions.txt -- left there, they are room for a real
regression to hide in."

pass "$(echo "$entries" | wc -w) entries x $(echo "$peers" | wc -w) protocols: every cross-protocol acceptance is one of the $(wc -l < "$work/expected.sorted") recorded"
