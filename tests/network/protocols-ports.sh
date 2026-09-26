#!/usr/bin/env bash
# The port a service is checked on comes from protocols.cfg, and nowhere else.
#
# getportnumber() used to fall through to the host's /etc/services, so an
# entry without a port line was checked on whatever that file said -- different
# between machines, and invisible to whoever reads this one. [amqps] had no
# port in any source and could only be tested by naming one per host.
#
# Every entry now declares its port. An entry may instead say in a comment why
# it has none, which nothing currently needs.

set -eu
. "$(dirname "$0")/../lib/assert.sh"
ROOT=$(find_root)
CFG="$ROOT/xymonnet/protocols.cfg"
[ -f "$CFG" ] || fail "cannot find xymonnet/protocols.cfg"

missing=$(awk '
	/^\[/    { if (name != "" && !port && !excused) print name; name = $0; port = 0; excused = 0 }
	/^[ \t]*port[ \t]/  { port = 1 }
	/No default port/   { excused = 1 }
	END      { if (name != "" && !port && !excused) print name }
' "$CFG")

[ -z "$missing" ] || fail \
"these entries declare no port, and nothing else supplies one -- the service
cannot be tested unless every host names a port for it. Add a port line, or a
comment saying why the service has no default:
$(sed 's/^/  /' <<<"$missing")"

# And where both define a port, they must agree: two sources disagreeing is
# how a probe ends up checking a port nobody chose.
NETSVC="$ROOT/lib/netservices.c"
if [ -f "$NETSVC" ]; then
	conflicts=""
	while read -r svc cport; do
		[ -n "$svc" ] || continue
		[ "$cport" = "0" ] && continue
		fport=$(awk -v s="$svc" '
			/^\[/ { inentry = ($0 ~ "^\\[" s "\\]" || $0 ~ "^\\[" s "\\|" || $0 ~ "\\|" s "\\]" || $0 ~ "\\|" s "\\|") }
			inentry && /^[ \t]*port[ \t]/ { print $2; exit }
		' "$CFG")
		[ -n "$fport" ] || continue
		[ "$fport" = "$cport" ] || conflicts="$conflicts
  $svc: protocols.cfg says $fport, lib/netservices.c says $cport"
	done <<-EOF
	$(sed -n '/^static svcinfo_t default_svcinfo/,/^};/p' "$NETSVC" |
	  sed -nE 's/^[[:space:]]*\{[[:space:]]*"([a-z0-9._-]+)",.*[^0-9]([0-9]+)[[:space:]]*\},?.*/\1 \2/p')
	EOF

	[ -z "$conflicts" ] || fail \
"protocols.cfg and the compiled name-to-port list disagree. Whichever is
consulted first wins, so the service is checked on a port that only one of the
two files names:$conflicts"
fi

# And the port must not come from the host. getservbyname()/getservbyport()
# read /etc/services, which varies between machines: a probe would then be
# checking a port this file does not name.
for src in xymonnet.c contest.c; do
	f="$ROOT/xymonnet/$src"
	[ -f "$f" ] || continue
	grep -vE '^[[:space:]]*(\*|/\*|//)' "$f" | grep -q 'getservby' && fail \
		"xymonnet/$src looks up a port in /etc/services. The port comes from
protocols.cfg; a host file deciding it means the probe checks a port this
tree does not name:
$(grep -n 'getservby' "$f" | grep -vE ':[[:space:]]*(\*|/\*|//)' | head -3)"
done

# --- and an entry's port must match the server its fixture came from --------
#
# protocol-realworld.sh rewrites each entry's port to its local peer, so the
# fixtures exercise the CONVERSATION and never the port. [submissiontls] shipped on
# 25 while both its transcripts were recorded from submission servers on 587,
# and nothing could see it.
#
# Recording on a different port is allowed -- several were taken over TLS
# because the plaintext port was unreachable -- but the header has to say so
# AND name the port the entry uses, so the difference is a decision rather
# than a mistake.
FIX="$ROOT/tests/fixtures/realworld"
if [ -d "$FIX" ]; then
	for peer in "$FIX"/*.peer; do
		[ -f "$peer" ] || continue
		hdr=$(head -1 "$peer")
		rport=$(sed -nE 's/^#[^:]*:([0-9]+).*/\1/p' <<<"$hdr")
		svc=$(sed -nE 's/.*drives \[([a-z0-9|._-]+)\].*/\1/p' <<<"$hdr" | cut -d'|' -f1)
		[ -n "$rport" ] && [ -n "$svc" ] || fail \
			"cannot read the host:port and service out of $(basename "$peer"):
$hdr"

		eport=$(awk -v s="$svc" '
			/^\[/ { inentry = ($0 ~ "^\\[" s "\\]" || $0 ~ "^\\[" s "\\|" || $0 ~ "\\|" s "\\]" || $0 ~ "\\|" s "\\|") }
			inentry && /^[ \t]*port[ \t]/ { print $2; exit }
		' "$CFG")
		[ -n "$eport" ] || fail \
			"$(basename "$peer") says it drives [$svc], which no entry defines"

		[ "$rport" = "$eport" ] && continue
		grep -q "identical on $eport" <<<"$hdr" || fail \
"$(basename "$peer") was recorded from port $rport, but [$svc] is checked on
$eport. Either the entry has the wrong port, or the header must say the
conversation is identical on $eport -- as the fixtures recorded over TLS do:
$hdr"
	done
fi

pass "every shipped entry declares its port, the compiled list agrees, no port comes from /etc/services, and each fixture was recorded from the port its entry uses"
