#!/usr/bin/env bash
#
# A protocols.cfg mistake should say so.
#
# Every one of these used to be silent. A mistyped directive was a no-op,
# so the step simply vanished and the probe reported on a conversation it
# never had; ${sha1:...} was read as a variable named "sha1:..." and
# expanded to nothing; a capture with no expect in front of it bound an
# empty string; starttls on a service that is already TLS started a second
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
   capture as tooearly
   expect "220"
   send "x${sha1:foo}\r\n"
   timeout 0
   starttls
   options ssl,banner
   port 9

[typo]
   expect "+OK"
   capture-regex "(<[^>]+>)" as challenge
   capture-regex "[0-9]+" as nogroup
   credentials mypop
   challeng ~ "<"              -> apop
   else                        -> plain
   state apop
   send "APOP ${username} ${md5:${challeng}${password}}\r\n"
   state plain
   send "USER ${usernam}\r\n"
   expect "+OK"
   port 9
CFG
out=$(run_xymonnet)

grep -qi 'unknown protocols.cfg directive' <<<"$out" || fail \
	"a mistyped directive was accepted in silence. The step is dropped and the
probe still runs, so the test reports on a conversation it never had:
$out"

grep -qi 'before any expect' <<<"$out" || fail \
	"'capture as' with no preceding expect was accepted; it can only bind an
empty value:
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
	"a capture-regex with no parenthesised group was accepted. The value bound
is group 1, so that pattern can never bind anything and \${name} expands to
nothing for every reply:
$out"

# --- and the shipped file must be quiet -------------------------------------
cp "$root/xymonnet/protocols.cfg" "$work/home/etc/protocols.cfg"
out=$(run_xymonnet)
noise=$(grep -ciE 'unknown protocols.cfg directive|before any expect|unknown expansion|already TLS|never captured or bound|can only fail|no capture group' <<<"$out" || true)
[ "$noise" -eq 0 ] || fail \
	"the protocols.cfg this tree ships triggers $noise of its own warnings:
$(grep -iE 'unknown|before any expect|already TLS' <<<"$out")"

pass "protocols.cfg mistakes are reported when the file is read, and the shipped file reports none"
