#!/usr/bin/env bash
#
# Branching on what the server said, and the values carried out of it.
#
# The peer recomputes what it expects rather than accepting what arrives:
# it derives md5(challenge + secret) and the base64 itself, so a probe that
# sends a wrong digest fails here instead of being waved through. That is
# the only way this proves anything -- a peer that just records bytes would
# pass on any hash at all.
#
# The same service definition meets two servers: one greeting WITH an APOP
# challenge and one without. The branch has to take a different arm for
# each, which is what makes it a branch rather than a straight line.

set -eu
. "$(dirname "$0")/../lib/assert.sh"
root=$(find_root)

require_bin XYMONNET xymonnet/xymonnet
require_cc

work=$(mktempdir); register_cleanup "rm -rf '$work'"
mkdir -p "$work/home/etc"

"$CC" -o "$work/peer" "$root/tests/lib/dialogue-peer.c" -lssl -lcrypto 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; skip "dialogue-peer does not compile"; }

printf 'mypop\ttestuser\ts3cret\n' > "$work/home/etc/credentials.cfg"
chmod 600 "$work/home/etc/credentials.cfg"

cat > "$work/apop.script" <<'EOS'
send "+OK POP3 mail.example.com <1896.697@test>\r\n"
md5check testuser s3cret
send "+OK logged in\r\n"
recv quit mail.example.com
send "+OK bye\r\n"
EOS
cat > "$work/plain.script" <<'EOS'
send "+OK POP3 ready\r\n"
recv USER testuser
send "+OK\r\n"
recv PASS s3cret
send "+OK\r\n"
recv quit
send "+OK bye\r\n"
EOS
cat > "$work/b64.script" <<'EOS'
send "+OK ready\r\n"
b64check AUTH_ testuser
send "+OK\r\n"
recv quit
send "+OK bye\r\n"
EOS

: > "$work/pids"
start() {
	"$work/peer" "$1" "$3" > "$2" &
	echo $! >> "$work/pids"
	local i=0
	while [ "$i" -lt 50 ]; do [ -s "$2" ] && break; sleep 0.1; i=$((i + 1)); done
	cat "$2"
}
pa=$(start "$work/apop.script"  "$work/pa" "$work/oa")
pp=$(start "$work/plain.script" "$work/pp" "$work/op")
pb=$(start "$work/b64.script"   "$work/pb" "$work/ob")
register_cleanup "kill $(tr '\n' ' ' < "$work/pids") 2>/dev/null || :"
[ -n "$pa" ] && [ -n "$pp" ] && [ -n "$pb" ] || fail "a peer never named its port"

# One definition, two servers: the branch decides which arm runs.
authblock() {
cat <<CFG
[$1]
   options banner
   port $2
   start greeting

   state greeting
      credentials mypop
      timeout(10)                 -> fail
      expect "+OK" as greeting    -> choose

   state choose
      greeting ~ "\\+OK POP3 (\\S+) (<[^>]+>)" as server;challenge
      challenge ~ "<"             -> apop
      else                        -> plain

   state apop
      send "APOP \${username} \${md5:\${challenge}\${password}}\r\n"
      timeout(10)                 -> fail
      expect "+OK"                -> farewell

   state plain
      send "USER \${username}\r\n"
      timeout(10)                 -> fail
      expect "+OK"                -> sendpass

   state sendpass
      send "PASS \${password}\r\n"
      timeout(10)                 -> fail
      expect "+OK"                -> farewell

   state farewell
      send "quit \${server}\r\n"
      timeout(10)                 -> fail
      eof                         -> success
      expect "+OK"                -> success
CFG
}
{
	authblock popapop "$pa"
	authblock popplain "$pp"
	cat <<CFG

[popb64]
   options banner
   port $pb

   state greeting
      credentials mypop
      timeout(10)                 -> fail
      expect "+OK"                -> auth

   state auth
      send "AUTH_\${base64:\${username}}\r\n"
      timeout(10)                 -> fail
      expect "+OK"                -> farewell

   state farewell
      send "quit\r\n"
      timeout(10)                 -> fail
      eof                         -> success
      expect "+OK"                -> success
CFG
} > "$work/home/etc/protocols.cfg"
printf '127.0.0.1\tapop\t# popapop\n127.0.0.1\tplain\t# popplain\n127.0.0.1\tb64\t# popb64\n' \
	> "$work/home/etc/hosts.cfg"

XYMONHOME="$work/home" "$XYMONNET" --no-update --noping --checkresponse \
	--dns=ip --timeout=15 >"$work/out.txt" 2>&1 || :
sleep 1

# Two names from one pattern, and a regex whose backslashes survive the
# config reader. \S used to be eaten before PCRE saw it -- the pattern
# became "(S+)", failed to compile, and every ${name} silently expanded to
# nothing. A regex is not a C string; it has its own escapes.
grep -qE 'no capture group|pcre compile' "$work/out.txt" && fail \
	"the capture-regex did not compile. Its backslashes are being consumed
before PCRE sees them, so the pattern is not the one that was written:
$(grep -iE 'pcre|capture' "$work/out.txt" | head -2)"

grep -q '^got quit mail.example.com' "$work/oa" || fail \
	"group 1 did not bind: 'as server;challenge' should give one name per
capture group, in order. The peer received:
$(cat "$work/oa")"

grep -q '^md5-ok' "$work/oa" || fail \
	"the APOP digest the probe sent is not md5(challenge + password). The peer
computed it independently:
$(cat "$work/oa")"

grep -q '^got USER testuser' "$work/op" || fail \
	"against a greeting with no challenge the else-arm did not run, so the
branch is not reacting to the captured value:
$(cat "$work/op")"
grep -q '^got PASS s3cret' "$work/op" || fail \
	"the else-arm stopped after USER; \${password} did not expand:
$(cat "$work/op")"

grep -q '^b64-ok' "$work/ob" || fail \
	"\${base64:\${username}} did not produce base64 of the username:
$(cat "$work/ob")"

for svc in "popapop on apop" "popplain on plain" "popb64 on b64"; do
	grep -q "Service $svc is OK" "$work/out.txt" || fail \
		"$svc did not report up:
$(cat "$work/out.txt")"
done

pass "capture, branching, credentials and \${md5:}/\${base64:} produce the values the server itself expects"
