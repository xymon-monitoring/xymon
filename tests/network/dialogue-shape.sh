#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# The shape rules, refused when the file is read.
#
# protocols.cfg(5) says a state does one thing and waits for one answer, that
# every expect names where it goes, and that a clock bounds the wait that
# follows it. Those were conventions the parser accepted any violation of, so
# a file could read as one machine and run as another: a clock below the
# expects bounds nothing, a second wait in a state has no name to fail under,
# and an expect with no target leaves the file silent about where the
# dialogue went.
#
# Every one of them is decidable while reading the file, which is the
# standard this grammar set for itself when it refused regex on expect. This
# checks that each is refused, by its own message, and that the service says
# so rather than running a test whose meaning nobody can state.
#
# THE CONTROL is [good]: the same conversation written the documented way. If
# it were refused too, these rules would be rejecting valid files and the
# messages below would prove nothing.

set -eu
. "$(dirname "$0")/../lib/assert.sh"
root=$(find_root)

require_bin XYMONNET xymonnet/xymonnet

work=$(mktempdir); register_cleanup "rm -rf '$work'"
mkdir -p "$work/home/etc"

cat > "$work/home/etc/protocols.cfg" <<'CFG'
[good]
   options banner
   port 1

   state greeting
      timeout(5)        -> fail
      expect "220"      -> ehlo

   state ehlo
      send "ehlo x\r\n"
      timeout(5)        -> fail
      expect "250"      -> success

[twoactions]
   # conventions: permissive -- broken on purpose, this suite is what refuses it
   port 1

   state one
      send "a\r\n"
      starttls
      timeout(5)        -> fail
      expect "220"      -> success

[actsagain]
   # conventions: permissive -- broken on purpose, this suite is what refuses it
   port 1

   state one
      timeout(5)        -> fail
      expect "220"      -> success
      send "b\r\n"

[clockbelow]
   # conventions: permissive -- broken on purpose, this suite is what refuses it
   port 1

   state one
      expect "220"      -> success
      timeout(5)        -> fail

[notarget]
   # conventions: permissive -- broken on purpose, this suite is what refuses it
   port 1

   state one
      timeout(5)        -> fail
      expect "220"

[reserved]
   # conventions: permissive -- broken on purpose, this suite is what refuses it
   port 1

   state success
      timeout(5)        -> fail
      expect "220"      -> success

[twooptions]
   # conventions: permissive -- broken on purpose, this suite is what refuses it
   options banner
   options ssl
   port 1

   state one
      timeout(5)        -> fail
      expect "220"      -> success

[lonelyuntil]
   # conventions: permissive -- broken on purpose, this suite is what refuses it
   port 1
   expect "250" until "250 "
CFG
{ for s in good twoactions actsagain clockbelow notarget reserved twooptions lonelyuntil; do
	printf '127.0.0.1\th-%s\t# %s\n' "$s" "$s"
  done; } > "$work/home/etc/hosts.cfg"

XYMONHOME="$work/home" "$XYMONNET" --no-update --noping --checkresponse=red \
	--dns=ip --timeout=5 >"$work/out.txt" 2>&1 || :

want() {	# pattern description
	grep -qi -- "$1" "$work/out.txt" || fail \
		"$2 was accepted. It is decidable while reading the file, so it has to be
refused there rather than left to behave differently from how it reads:
$(head -25 "$work/out.txt")"
}

want "more than one action"                "a state with two actions"
want "acts again after it has waited"      "a state that sends after it has waited"
want "clock below its expects"             "a clock written below the expects it should bound"
want "expect with no "                     "an expect that names no state"
want "reserved for a verdict"              "a state named 'success'"
want "second 'options' line"               "a second options line replacing the first"
want "does not run"                        "'until' on an entry the driver never runs"

# THE CONTROL: the documented shape must not be caught by any of this.
grep -qi "Service good" "$work/out.txt" || fail \
	"the control entry produced no result at all, so this run proves nothing"
grep -Ei "state '(greeting|ehlo)'" "$work/out.txt" | grep -Eqi "more than one action|no '\-> TARGET'|clock below" && fail \
	"the entry written exactly as protocols.cfg(5) documents was refused:
$(grep -i good "$work/out.txt" | head -5)"

pass "a misshapen state is refused when the file is read, and the documented shape is not"
