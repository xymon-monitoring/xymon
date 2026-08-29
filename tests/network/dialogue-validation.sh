#!/usr/bin/env bash
#
# A protocols.cfg mistake should say so.
#
# Every one of these used to be silent. A mistyped directive was a no-op,
# so the step simply vanished and the probe reported on a conversation it
# never had; ${sha1:...} was read as a variable named "sha1:..." and
# expanded to nothing; an extraction naming a value that nothing
# binds read an empty string; starttls on a service that is already TLS started a second
# handshake inside the first and failed like a broken server.
#
# The checks run when the file is read, not when the step executes -- a
# step that is never reached would otherwise never report its own mistake.
# That is why this test never connects to anything.
#
# The second half matters as much as the first: the config the tree SHIPS
# must produce none of these. A validator that cries wolf gets ignored,
# and then the real warnings go unread too.

set -eu
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

# --- a file with one of each mistake ----------------------------------------
printf 'mypop\tuser\tsecret\n' > "$work/home/etc/credentials.cfg"
cat > "$work/home/etc/protocols.cfg" <<'CFG'
[bad]
   exepct "220"
   expect "220"
   nosuch ~ "(x)"              as tooearly
   send "x${sha1:foo}\r\n"
   timeout(0)                  -> fail
   starttls
   options ssl,banner
   port 9

[typo]
   port 9
   port 9
   expect "+OK" as greeting
   greeting ~ "(<[^>]+>)" as challenge
   greeting ~ "[0-9]+" as nogroup
   credentials mypop
   challeng ~ "<"              -> apop
   else                        -> plain

   state apop
      send "APOP ${username} ${md5:${challeng}${password}}\r\n"

   state plain
      send "USER ${usernam}\r\n"
      expect "+OK"

[graph]
   port 9
   start entry

   state entry
      expect "220"                -> spin
      timeout(5)                  -> fail

   state spin
      send "x\r\n"
      expect "250"                -> spin

   state unreachable
      send "y\r\n"
      expect "250"                -> fail
      timeout(5)                  -> fail

CFG
out=$(run_xymonnet)

grep -qi 'unknown protocols.cfg directive' <<<"$out" || fail \
	"a mistyped directive was accepted in silence. The step is dropped and the
probe still runs, so the test reports on a conversation it never had:
$out"

grep -qi 'never bound before it' <<<"$out" || fail \
	"an extraction reading a name that nothing binds was accepted; it can only
ever bind an empty value:
$out"

grep -qi "state 'spin' waits for a reply with no timeout" <<<"$out" || fail \
	"a state that waits with no timer was accepted. It can only ever fail as
the global timeout, which cannot say which state stalled -- the complaint
this feature exists to answer:
$out"

grep -qi "state 'spin' has no way to finish" <<<"$out" || fail \
	"a state whose every path leads back into itself was accepted; the
dialogue can never end except on the ceiling:
$out"

grep -qi "state 'unreachable' cannot be reached" <<<"$out" || fail \
	"a state nothing can reach was accepted. That is dead config, and the
'-> NAME has no matching state' check does not catch it because the name
resolves fine -- nothing simply names it:
$out"

grep -qi 'the budget must be a positive number of seconds' <<<"$out" || fail \
	"'timeout 0' was accepted. A zero or negative budget can only expire
immediately or never, and both are silent at run time:
$out"

grep -qi 'unknown expansion' <<<"$out" || fail \
	"\${sha1:...} was accepted. It is not a function, so it is read as a
variable of that name and expands to nothing:
$out"

grep -qi 'already TLS' <<<"$out" || fail \
	"'starttls' together with 'options ssl' was accepted. That runs a second
handshake inside the first:
$out"

grep -q 'never captured or bound' <<<"$out" || fail \
	"a \${name} that nothing binds was accepted. It expands to nothing, the
command goes out malformed, and the test then fails for a reason that has
nothing to do with the typo:
$out"

grep -qi 'can only fail' <<<"$out" || fail \
	"a '~' edge testing a value nothing captures was accepted; it can only
ever fall through to the else-arm:
$out"

grep -q 'no capture group' <<<"$out" || fail \
	"an extraction with no parenthesised group was accepted. The value bound
is group 1, so that pattern can never bind anything and \${name} expands to
nothing for every reply:
$out"

# --- and the shipped file must be quiet -------------------------------------
cp "$root/xymonnet/protocols.cfg" "$work/home/etc/protocols.cfg"
out=$(run_xymonnet)
noise=$(grep -ciE 'unknown protocols.cfg directive|never bound before it|unknown expansion|already TLS|never captured or bound|can only fail|no capture group' <<<"$out" || true)
[ "$noise" -eq 0 ] || fail \
	"the protocols.cfg this tree ships triggers $noise of its own warnings:
$(grep -iE 'unknown|before any expect|already TLS' <<<"$out")"

pass "protocols.cfg mistakes are reported when the file is read, and the shipped file reports none"
