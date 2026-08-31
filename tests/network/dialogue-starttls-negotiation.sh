#!/usr/bin/env bash
#
# What happens when the upgrade does NOT succeed.
#
# dialogue-tls.sh covers a STARTTLS that works, and dialogue-terminals.sh
# covers "-> warning" on its own. Neither covers the two together, which
# is the combination an operator actually deploys: a mail server that
# stopped offering STARTTLS, or that answers 454 because its certificate
# file went missing, is not down. It answers every command correctly. A
# probe that calls that red gets switched off; a probe that calls it green
# never noticed. Yellow is the whole point of the feature, so it is worth
# a test that it is really yellow and not merely "not green".
#
# THE CONTROL is --checkresponse=red: with a plain dialogue failure
# reporting red, a yellow result can only have come from an edge that says
# "-> warning". Without it these assertions would also pass on a probe
# that failed for an unrelated reason, since yellow is the default colour
# for a response mismatch.
#
# The last two entries are the same conversation with the two expect lines
# written in the opposite order. They must produce the SAME message. They
# did not: the report blamed the head of the alternatives group rather than
# the alternative that matched, so a server answering "454" was reported
# as 'expected "220"' -- and swapping two lines that changed nothing about
# the test changed what an operator was told to go and look at.

set -eu
. "$(dirname "$0")/../lib/assert.sh"
root=$(find_root)

require_bin XYMONNET xymonnet/xymonnet
require_cc
command -v openssl >/dev/null 2>&1 || skip "openssl is needed to make a test certificate"

work=$(mktempdir); register_cleanup "rm -rf '$work'"
mkdir -p "$work/home/etc"

# A name nothing else in this run uses, so finding it proves the
# certificate came off the session that started in plaintext.
openssl req -x509 -newkey rsa:2048 -keyout "$work/k.pem" -out "$work/c.pem" \
	-days 2 -nodes -subj "/CN=negotiated.example" >"$work/ssl.log" 2>&1 \
	|| skip "openssl could not generate a test certificate"

"$CC" -o "$work/peer" "$root/tests/lib/dialogue-peer.c" -lssl -lcrypto 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; skip "dialogue-peer does not compile against libssl"; }

# Offers STARTTLS and completes the upgrade.
cat > "$work/up.script" <<'EOS'
send "220 test.local ESMTP\r\n"
recv ehlo
send "250-test.local\r\n250 STARTTLS\r\n"
recv starttls
send "220 2.0.0 Ready to start TLS\r\n"
starttls
recv ehlo
send "250-test.local\r\n250 OK\r\n"
recv quit
send "221 bye\r\n"
EOS

# Answers everything correctly, but never advertises STARTTLS.
cat > "$work/none.script" <<'EOS'
send "220 test.local ESMTP\r\n"
recv ehlo
send "250-test.local\r\n250 SIZE 10240000\r\n"
hold 20
EOS

# Advertises STARTTLS and then declines it -- what a server with an
# unreadable certificate file does.
cat > "$work/refuse.script" <<'EOS'
send "220 test.local ESMTP\r\n"
recv ehlo
send "250-test.local\r\n250 STARTTLS\r\n"
recv starttls
send "454 4.7.0 TLS not available due to local problem\r\n"
hold 20
EOS

: > "$work/pids"
start() {	# script portfile obsfile
	"$work/peer" "$1" "$3" "$work/c.pem" "$work/k.pem" > "$2" &
	echo $! >> "$work/pids"
	local i=0
	while [ "$i" -lt 50 ]; do [ -s "$2" ] && break; sleep 0.1; i=$((i + 1)); done
	cat "$2"
}
pup=$(start "$work/up.script"     "$work/pup" "$work/oup")
pno=$(start "$work/none.script"   "$work/pno" "$work/ono")
pra=$(start "$work/refuse.script" "$work/pra" "$work/ora")
prb=$(start "$work/refuse.script" "$work/prb" "$work/orb")
register_cleanup "kill $(tr '\n' ' ' < "$work/pids") 2>/dev/null || :"
for p in "$pup" "$pno" "$pra" "$prb"; do
	[ -n "$p" ] || fail "a peer never named its port"
done

# tlsup is the worked example from protocols.cfg(5), verbatim. If this
# entry stops working the manual is wrong, which is worse than a bug.
#
# The two entries spell the same step differently on purpose. [tlsup] writes
# all four clauses on one line -- expect, until, as, and the edge -- which is
# the form the manual prints under "expect ... as NAME" and which nothing else
# in the suite covers, and every expect in it ends its state by naming the
# next. [tlsnone] uses the permissive spelling the parser also accepts, where
# an expect names no target and falls through to the step below it. Both must
# reach the same verdicts, or one of the two is documented and not
# implemented.
cat > "$work/home/etc/protocols.cfg" <<CFG
[tlsup]
   options banner
   port $pup

   state greeting
      timeout(10)                        -> fail
      expect "220"                       -> ehlo

   state ehlo
      send "ehlo xymonnet\r\n"
      timeout(10)                        -> fail
      expect "250" until "250 " as caps  -> offers

   state offers
      caps ~ "STARTTLS"                  -> upgrade
      else                               -> warning

   state upgrade
      send "starttls\r\n"
      timeout(10)                        -> fail
      expect "220"                       -> secure
      expect "454"                       -> warning

   state secure
      start tls
      always                             -> ehlo-tls

   state ehlo-tls
      send "ehlo xymonnet\r\n"
      timeout(10)                        -> fail
      expect "250" until "250 "          -> farewell

   state farewell
      send "quit\r\n"
      timeout(10)                        -> fail
      expect "221"                       -> success

[tlsnone]
   options banner
   port $pno

   state greeting
      timeout(10)                        -> fail
      expect "220"                       -> ehlo

   state ehlo
      send "ehlo xymonnet\r\n"
      timeout(10)                        -> fail
      expect "250" until "250 " as caps  -> offers

   state offers
      caps ~ "STARTTLS"                  -> upgrade
      else                               -> warning

   state upgrade
      send "starttls\r\n"
      timeout(10)                        -> fail
      expect "220"                       -> secure
      expect "454"                       -> warning

   state secure
      start tls
      always                             -> farewell

   state farewell
      send "quit\r\n"
      timeout(10)                        -> fail
      expect "221"                       -> success

[tls454a]
   options banner
   port $pra

   state greeting
      timeout(10)                        -> fail
      expect "220"                       -> ehlo

   state ehlo
      send "ehlo xymonnet\r\n"
      timeout(10)                        -> fail
      expect "250" until "250 " as caps  -> offers

   state offers
      caps ~ "STARTTLS"                  -> upgrade
      else                               -> warning

   state upgrade
      send "starttls\r\n"
      timeout(10)                        -> fail
      expect "220"                       -> secure
      expect "454"                       -> warning

   state secure
      start tls
      always                             -> farewell

   state farewell
      send "quit\r\n"
      timeout(10)                        -> fail
      expect "221"                       -> success

[tls454b]
   options banner
   port $prb

   state greeting
      timeout(10)                        -> fail
      expect "220"                       -> ehlo

   state ehlo
      send "ehlo xymonnet\r\n"
      timeout(10)                        -> fail
      expect "250" until "250 " as caps  -> offers

   state offers
      caps ~ "STARTTLS"                  -> upgrade
      else                               -> warning

   state upgrade
      send "starttls\r\n"
      timeout(10)                        -> fail
      expect "454"                       -> warning
      expect "220"                       -> secure

   state secure
      start tls
      always                             -> farewell

   state farewell
      send "quit\r\n"
      timeout(10)                        -> fail
      expect "221"                       -> success

CFG

printf '127.0.0.1\tup\t# tlsup\n127.0.0.1\tnone\t# tlsnone\n' > "$work/home/etc/hosts.cfg"
printf '127.0.0.1\trefa\t# tls454a\n127.0.0.1\trefb\t# tls454b\n' >> "$work/home/etc/hosts.cfg"

# --checkresponse=red is the control: see the header.
XYMONHOME="$work/home" "$XYMONNET" --no-update --noping --checkresponse=red \
	--dns=ip --timeout=20 >"$work/out.txt" 2>&1 || :
sleep 1

colour_of() {	# host.service -> the colour xymonnet reported
	grep -oE "status\+[0-9]+ $1 (green|yellow|red|clear)" "$work/out.txt" |
		awk '{print $3}' | head -1
}
message_of() {	# service host -> the failure text
	sed -n "s/^Service $1 on $2 is not OK : //p" "$work/out.txt" | head -1
}

# A state named only by a "~" edge, in an entry whose states budget their
# waits, must not be reported unreachable: a timeout edge leaves the state
# but the state continues past it, so the walk must not stop there. Every
# entry below is shaped that way, and the manual's example is too.
grep -q 'cannot be reached' "$work/out.txt" && fail \
	"a state that an edge plainly names was reported unreachable, so the
config checker is contradicting a working entry:
$(grep 'cannot be reached' "$work/out.txt")"

# --- the upgrade succeeds ----------------------------------------------------
grep -q '^tls-ok' "$work/oup" || fail \
	"the offered upgrade never completed: $(cat "$work/oup")"
[ "$(colour_of up.tlsup)" = green ] || fail \
	"a server that offers STARTTLS and completes it should be green, got '$(colour_of up.tlsup)':
$(grep -i tlsup "$work/out.txt" | head -4)"
grep -q 'CN=negotiated.example' "$work/out.txt" || fail \
	"no certificate was read off the upgraded session, so the probe reported
an upgraded service while knowing nothing about the certificate protecting it:
$(grep -iE 'certinfo|CN=' "$work/out.txt" | head -4)"

# --- STARTTLS is not offered -------------------------------------------------
grep -q '^got starttls' "$work/ono" && fail \
	"the probe sent STARTTLS to a server that never advertised it. The
capability test is not reading the EHLO reply:
$(cat "$work/ono")"
[ "$(colour_of none.tlsnone)" = yellow ] || fail \
	"a server that answers correctly but offers no STARTTLS should be yellow
via 'else -> warning'. With --checkresponse=red a plain failure would be red,
so '$(colour_of none.tlsnone)' means the else-arm did not decide this:
$(grep -i tlsnone "$work/out.txt" | head -4)"

# --- STARTTLS is refused -----------------------------------------------------
for h in refa refb; do
	svc=tls454a; [ "$h" = refb ] && svc=tls454b
	[ "$(colour_of $h.$svc)" = yellow ] || fail \
		"a 454 answer to STARTTLS should be yellow via 'expect \"454\" -> warning',
got '$(colour_of $h.$svc)' with plain failures set to red:
$(grep -i "$svc" "$work/out.txt" | head -4)"
done

# The message must name the alternative that MATCHED, not whichever one
# happens to be written first.
ma=$(message_of tls454a refa)
mb=$(message_of tls454b refb)
case "$ma" in
	*454*) : ;;
	*) fail "the report does not mention the 454 the server actually sent: '$ma'" ;;
esac
case "$ma" in
	*'"220"'*) fail \
		"the server answered 454 and the report blames the 220 alternative
instead: '$ma'. An operator reading this goes looking for a broken
greeting on a server whose greeting was fine." ;;
esac
[ "$ma" = "$mb" ] || fail \
	"writing the two alternatives in the opposite order changed the report,
though it changed nothing about the test:
   220 first: '$ma'
   454 first: '$mb'"

pass "a STARTTLS that is not offered or is refused reports yellow, and names the answer that decided it"
