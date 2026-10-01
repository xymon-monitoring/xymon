#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/network/protocol-realworld.sh
#
# The entries this tree ships, against what real servers actually say.
#
# Every other dialogue test drives a peer whose lines a test author wrote, so
# it checks the driver against an idea of the protocol. This one replays
# transcripts recorded from public servers -- gmail's MX and submission hosts,
# ftp.gnu.org, github.com -- so the shipped entries meet the wording those
# servers really use: multi-line 250s with the terminator only on the last,
# an IMAP LOGOUT answered by an untagged "* BYE" and then a tagged "OK", the
# exact closing codes.
#
# LAYER: the shipped protocols.cfg, unmodified, end to end. The port comes
# from hosts.cfg so the file under test is the file we install.
#
# The transcripts are kept beside the scripts in tests/fixtures/realworld/,
# so a failure can be read against what the server said rather than guessed at.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

root=$(find_root)
require_bin XYMONNET xymonnet/xymonnet
: "${CC:=cc}"
command -v "$CC" >/dev/null 2>&1 || skip "no C compiler available (CC=$CC)"
command -v openssl >/dev/null 2>&1 || skip "openssl CLI needed for the STARTTLS fixture"

fix="$root/tests/fixtures/realworld"
[ -d "$fix" ] || skip "no realworld fixtures in this tree"

work=$(mktempdir); register_cleanup "rm -rf '$work'"
mkdir -p "$work/home/etc"

openssl req -x509 -newkey rsa:2048 -keyout "$work/key.pem" -out "$work/cert.pem" \
	-days 30 -nodes -subj "/CN=mail.test.local" >"$work/ssl.log" 2>&1 \
	|| skip "openssl could not generate a test certificate"

"$CC" -o "$work/peer" "$root/tests/lib/dialogue-peer.c" -lssl -lcrypto 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; skip "dialogue-peer does not compile against libssl"; }

: > "$work/pids"
start_peer() {	# script portfile obsfile
	"$work/peer" "$1" "$3" "$work/cert.pem" "$work/key.pem" > "$2" &
	echo $! >> "$work/pids"
	i=0
	while [ "$i" -lt 60 ]; do [ -s "$2" ] && break; sleep 0.1; i=$((i + 1)); done
	cat "$2"
}

# One fixture per file, named <service>@<origin>.peer, so a single entry can be
# held to several servers' wording. The service is the name hosts.cfg uses.
fixtures=$(cd "$fix" && ls *.peer 2>/dev/null | sort)
[ -n "$fixtures" ] || skip "no .peer fixtures found"

: > "$work/home/etc/hosts.cfg"
: > "$work/cases"
for f in $fixtures; do
	svc=${f%%@*}
	origin=${f##*@}; origin=${origin%.peer}
	host=$(printf '%s%s' "$svc" "$origin" | tr -cd 'a-z0-9')
	p=$(start_peer "$fix/$f" "$work/port.$host" "$work/obs.$host")
	[ -n "$p" ] || skip "the peer for $f never named its port"
	printf '127.0.0.1\t%s\t# %s:%s\n' "$host" "$svc" "$p" >> "$work/home/etc/hosts.cfg"
	printf '%s %s %s\n' "$host" "$svc" "$f" >> "$work/cases"
done
register_cleanup "kill $(tr '\n' ' ' < "$work/pids") 2>/dev/null || :"

# THE FILE UNDER TEST: the one this tree ships, copied unmodified.
cp "$root/xymonnet/protocols.cfg" "$work/home/etc/protocols.cfg"

XYMONHOME="$work/home" "$XYMONNET" --no-update --noping --checkresponse=red \
	--dns=ip --timeout=30 >"$work/out.txt" 2>&1 || :

colour_of() { grep -oE "status\+[0-9]+ $1 (green|yellow|red|clear)" "$work/out.txt" | awk '{print $3}' | head -1; }

while read -r host svc f; do
	got=$(colour_of "${host}\.${svc}")
	[ "$got" = green ] || fail \
"the shipped [$svc] entry did not go green against $f, a transcript recorded
from a real server (got '${got:-no status}'). The server's own words are in
tests/fixtures/realworld/transcripts/, and this is what the peer saw:
$(sed 's/^/  /' "$work/obs.$host" 2>/dev/null | head -8)
$(grep -i "$host" "$work/out.txt" | head -3)"

	# Green is not the whole of it. The fixtures use "recv PREFIX", so the peer
	# already records MISMATCH when the command it was handed is not the one the
	# transcript expects -- and nothing was reading that.
	! grep -q '^MISMATCH' "$work/obs.$host" || fail \
"the shipped [$svc] entry went green against $f while sending something the
real server was not asked for. The colour only says the reply matched; the
peer recorded what actually went over the wire:
$(grep '^MISMATCH' "$work/obs.$host" | sed 's/^/  /' | head -5)"

	# And it must not stop SHORT. Each .peer is trimmed to what its entry does
	# and ends in "hangup", so the peer reaching EOF while waiting for a command
	# means the probe closed with the conversation unfinished. That is invisible
	# in the colour: an entry that ends early just runs out of steps and reports
	# up, which is how [submissiontls] stopping at the handshake looked green.
	! grep -q '^eof' "$work/obs.$host" || fail \
"the shipped [$svc] entry went green against $f while leaving the conversation
unfinished. The peer was still waiting for a command when the probe closed,
so the entry does less than the transcript it is checked against:
$(sed 's/^/  /' "$work/obs.$host" | tail -4)"
done < "$work/cases"

# The upgrade is the one that cannot be inferred from a plaintext transcript:
# [submissiontls] must reach a certificate, which a plaintext port has none of until
# STARTTLS has happened.
# Every fixture that hands over a certificate must reach the sslcert column,
# and the two ways of getting one are both here: a script whose FIRST command
# is "starttls" is a server that is TLS from the first byte ("options ssl"),
# and one where it comes later is an upgrade in the middle of a plaintext
# conversation, which is the certificate a plaintext port has no other way to.
while read -r host svc f; do
	grep -q '^starttls$' "$fix/$f" || continue
	# the first line that is neither blank nor a comment
	first=$(grep -vE '^[[:space:]]*(#|$)' "$fix/$f" | head -1)
	[ "$first" = starttls ] && how="at connect" || how="after the upgrade"
	grep -qE "status\+[0-9]+ $host\.sslcert " "$work/out.txt" || fail \
"[$svc] completed against $f but sent no sslcert status, so the certificate
was never read $how:
$(grep -iE "sslcert|$host" "$work/out.txt" | head -5)"
done < "$work/cases"

pass "the shipped entries hold their conversation with what real servers say, and the upgrade reaches a certificate"
