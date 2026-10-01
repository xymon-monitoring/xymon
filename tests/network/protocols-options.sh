#!/usr/bin/env bash
#
# The list of options is written in three places. They have to agree.
#
# The parser in lib/netservices.c decides what is accepted; protocols.cfg(5)
# and the header of protocols.cfg tell the operator what to write. An option
# missing from either document is one nobody will use, and an option listed
# but not accepted is worse: since an unrecognised option refuses the whole
# entry, following the documentation would take the service down.
#
# This has drifted twice already -- "connect-only" and then "alpn=" were each
# added to the parser and the manual but missed out of the file's own header,
# which is the copy an operator actually has open while editing.
#
# Names only. What each option DOES is prose, and prose that differs between
# the manual and the file is a matter for a reader, not for a test.

set -eu
. "$(dirname "$0")/../lib/assert.sh"
ROOT=$(find_root)

SRC="$ROOT/lib/netservices.c"
MAN="$ROOT/xymonnet/protocols.cfg.5"
CFG="$ROOT/xymonnet/protocols.cfg"
for f in "$SRC" "$MAN" "$CFG"; do [ -f "$f" ] || fail "cannot find $f"; done

# What the parser accepts: strcmp(opt, "x") for plain options, strncmp for the
# ones that carry a value. Taken from the options loop only, so an unrelated
# strcmp elsewhere in the file cannot add to the list.
parser=$(sed -n '/opt = strtok(l, ",")/,/^			}$/p' "$SRC" |
	 grep -oE 'strn?cmp\(opt, "[a-z-]+=?"' |
	 sed -E 's/.*"([a-z-]+)=?"/\1/' | sort -u)

# What the manual lists: the indented block under "The possible options are".
man=$(sed -n '/The possible options are/,/^\.fi/p' "$MAN" |
      sed -nE 's/^\\&[[:space:]]+([a-z-]+)(=[^ ]*)? - .*/\1/p' | sort -u)

# What the file's own header lists: one option per line, name first.
cfg=$(sed -n '/^# options one or more of/,/^#$/p' "$CFG" |
      sed -nE 's/^#   ([a-z-]+)(=[^ ]*)?[[:space:]]+[^ ].*/\1/p' | sort -u)

[ -n "$parser" ] || fail "could not read the accepted options out of lib/netservices.c"
[ -n "$man" ]    || fail "could not read the documented options out of protocols.cfg.5"
[ -n "$cfg" ]    || fail "could not read the documented options out of protocols.cfg"

report() {  # report <what> <missing-list>
	[ -z "$2" ] || fail \
"the option lists disagree: $1
  parser (lib/netservices.c):  $(tr '\n' ' ' <<<"$parser")
  manual (protocols.cfg.5):    $(tr '\n' ' ' <<<"$man")
  header (protocols.cfg):      $(tr '\n' ' ' <<<"$cfg")
  difference:                  $(tr '\n' ' ' <<<"$2")"
}

report "accepted by the parser but documented nowhere in the manual" "$(comm -23 <(echo "$parser") <(echo "$man"))"
report "in the manual but NOT accepted -- following the manual would refuse the entry" "$(comm -13 <(echo "$parser") <(echo "$man"))"
report "accepted by the parser but missing from the file's own header" "$(comm -23 <(echo "$parser") <(echo "$cfg"))"
report "in the file's header but NOT accepted -- following it would refuse the entry" "$(comm -13 <(echo "$parser") <(echo "$cfg"))"

# The three that a step now says better. All are still accepted -- they are in
# files people already have, and an unknown option refuses the entry, so
# dropping them would take those services down on upgrade. But nothing shipped
# should use them: an option states that something happens, a step states
# where, and for these three the where is what matters.
#
#   ssl     -> "start tls" as the first step (handshake before anything else)
#   telnet  -> "start iac"
#   banner  -> an "expect", which reads AND asks for an answer
legacy_used=$(awk '
	/^\[/ { name = $0; sub(/^\[/, "", name); sub(/\|.*/, "", name); sub(/\].*/, "", name) }
	/^[[:space:]]*options/ {
		line = $0
		if (line ~ /(^|[, ])ssl([, ]|$)/)    print name ": ssl"
		if (line ~ /(^|[, ])telnet([, ]|$)/) print name ": telnet"
		if (line ~ /(^|[, ])banner([, ]|$)/) print name ": banner"
	}
' "$ROOT/xymonnet/protocols.cfg")

[ -z "$legacy_used" ] || fail \
"a shipped entry still uses an option a step replaces:
$(printf '%s\n' "$legacy_used" | sed 's/^/  /')
These stay accepted for existing files, but the shipped entries say it with
steps -- 'start tls', 'start iac', or an 'expect' that asks for an answer
instead of taking whatever the first read happened to hold."

pass "the options the parser accepts, the manual documents and protocols.cfg's header lists are the same set: $(tr '\n' ' ' <<<"$parser"), and no shipped entry uses one a step replaces"
