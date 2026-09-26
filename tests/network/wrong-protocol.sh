#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/network/wrong-protocol.sh
#
# A probe for one protocol, against a server speaking another, must go red.
#
# That is the whole claim a service column makes: not "something is listening
# here" but "the thing listening here is an SMTP server". A port outlives the
# service that opened it -- decommissioned hosts get reused, load balancers get
# repointed, a container lands on the address something else used to hold --
# and an entry that accepts any answer keeps reporting green through all of it.
#
# Every shipped entry is pointed at one peer that answers HTTP and nothing else.
# HTTP is the useful wrong protocol here: it is what actually turns up on a
# reused port, it answers rather than staying silent (silence any entry catches),
# and it is text, so an entry matching a text banner has every chance to be
# fooled by it.
#
# The entries that CANNOT yet tell the difference are listed below with the
# reason. The list is checked in both directions: an entry not on it that goes
# green fails, and an entry ON it that goes red fails as well, so the list
# cannot rot -- it can only shrink, and shrinking it means editing this file
# deliberately.
#
# LAYER: every entry in the shipped configuration, driven end to end.

set -eu
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

root=$(find_root)
require_bin XYMONNET xymonnet/xymonnet
: "${CC:=cc}"
command -v "$CC" >/dev/null 2>&1 || skip "no C compiler: every entry below needs a peer"

work=$(mktempdir); register_cleanup "rm -rf '$work'"
mkdir -p "$work/home/etc"

"$CC" -o "$work/peer" "$root/tests/lib/dialogue-peer.c" -lssl -lcrypto 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; skip "dialogue-peer does not compile against libssl"; }

# Entries that cannot yet reject a wrong protocol, and why. Each is a service
# whose column says less than it appears to.
#
#   ldap lpd netbios-ssn qmtp qmqp  no conversation at all -- the port opening
#                                   is the entire check, so anything listening
#                                   satisfies it
#   telnet                          asks only that the server spoke, by design:
#                                   the banner is the payload, and there is no
#                                   fixed text a telnet server must send
#   cupsd                           NOT a weakness -- IPP runs over HTTP, so the
#                                   peer below speaks its protocol properly and
#                                   green is the right answer
cannot_tell="ldap lpd netbios-ssn qmtp qmqp telnet cupsd"

# One peer per entry: a peer serves a single connection.
printf 'send "HTTP/1.1 200 OK\\r\\nServer: nginx\\r\\nContent-Length: 0\\r\\n\\r\\n"\nhangup\n' \
	> "$work/http.script"

: > "$work/pids"
services=$(sed -n 's/^\[\([^]|]*\).*\]$/\1/p' "$root/xymonnet/protocols2.cfg" | LC_ALL=C sort -u)
[ -n "$services" ] || fail "no service names could be read out of protocols2.cfg"

printf 'page test T\n' > "$work/home/etc/hosts.cfg"
for svc in $services; do
	"$work/peer" "$work/http.script" "$work/obs.$svc" > "$work/port.$svc" 2>/dev/null &
	echo $! >> "$work/pids"
done
register_cleanup "kill \$(tr '\\n' ' ' < '$work/pids') 2>/dev/null || :"

i=0
while [ "$i" -lt 60 ]; do
	ready=1
	for svc in $services; do [ -s "$work/port.$svc" ] || ready=0; done
	[ "$ready" = 1 ] && break
	sleep 0.1; i=$((i + 1))
done

started=""
for svc in $services; do
	port=$(cat "$work/port.$svc" 2>/dev/null || :)
	[ -n "$port" ] || continue
	printf '127.0.0.1\th%s\t# %s:%s\n' "$svc" "$svc" "$port" >> "$work/home/etc/hosts.cfg"
	started="$started $svc"
done
[ -n "$started" ] || skip "no peer named its port"

cp "$root/xymonnet/protocols.cfg"  "$work/home/etc/protocols.cfg"
cp "$root/xymonnet/protocols2.cfg" "$work/home/etc/protocols2.cfg"

XYMONHOME="$work/home" "$XYMONNET" --no-update --noping --checkresponse=red \
	--dns=ip --timeout=8 >"$work/out.txt" 2>&1 || :

colour_of() { grep -oE "status\+[0-9]+ h$1\.[a-z0-9_|-]+ (green|yellow|red|clear)" "$work/out.txt" \
		| awk '{print $3}' | head -1; }

fooled="" stale="" missing=""
for svc in $started; do
	got=$(colour_of "$svc")
	[ -n "$got" ] || { missing="$missing $svc"; continue; }

	listed=0
	case " $cannot_tell " in *" $svc "*) listed=1 ;; esac

	if [ "$got" = green ] && [ "$listed" = 0 ]; then
		fooled="$fooled
  $svc"
	elif [ "$got" != green ] && [ "$listed" = 1 ]; then
		stale="$stale
  $svc (now reports $got)"
	fi
done

[ -z "$missing" ] || fail \
"no result was reported for:$missing
The run did not reach these entries at all, so nothing below was measured:
$(head -5 "$work/out.txt")"

[ -z "$fooled" ] || fail \
"these entries reported the service UP against a peer that answered in HTTP and
nothing else:$fooled
The column claims a named service is healthy, so accepting a reply from some
other protocol makes it say something it cannot know. Give the entry something
to check, or add it to \"cannot_tell\" above with the reason it cannot."

[ -z "$stale" ] || fail \
"these entries are listed as unable to tell a wrong protocol apart, but now
do:$stale
That is good news and the list is out of date. Remove them from
\"cannot_tell\", so the next entry that loses the ability is noticed."

pass "$(echo "$started" | wc -w) entries: a wrong-protocol server is rejected by all but the $(echo "$cannot_tell" | wc -w) that cannot yet"
