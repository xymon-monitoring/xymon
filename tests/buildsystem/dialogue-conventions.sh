#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/buildsystem/dialogue-conventions.sh
#
# protocols.cfg(5) states how a dialogue entry is written, and the parser
# enforces none of it: an attribute is accepted anywhere in the block,
# indentation is ignored, and several actions or several waits in one state
# parse fine. The conventions are what make an entry readable as the machine
# it describes, so nothing but a test keeps them true -- and prose that
# nothing checks drifts, which is how the manual came to document a "when ...
# end" grammar that no shipped code ever had.
#
# This checks every entry we publish or test against the rules the manual
# lists under CONVENTIONS:
#
#   R1  entry attributes (options, port, transport, start, framing) come before the
#       first state, since they describe the definition and not a step
#   R2  a state's lines are indented under it
#   R3  a state holds at most one action (send, starttls, credentials)
#   R4  every expect in a state names where it goes: "-> TARGET"
#   R5  a clock (timeout, idle) is written above the expects it bounds,
#       because it arms the wait that FOLLOWS it and bounds nothing below them
#   R6  no directive that was tried and dropped: when/end, capture,
#       capture-regex, expect-regex, goto, "-> green"
#
# Sources: the shipped protocols.cfg, the examples in protocols.cfg.5 and in
# docs/design/protocol-dialogues.md, and the protocols.cfg written by every
# dialogue test. An entry that means to break a rule says so in the file with
#
#   # conventions: permissive
#
# which is how the suite keeps covering the spellings the parser accepts but
# the manual does not recommend.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
cd "$ROOT"

work=$(mktempdir); register_cleanup "rm -rf '$work'"
stream="$work/entries.txt"        # FILE<TAB>LINENO<TAB>TEXT
: > "$stream"

# --- extract config text from each kind of source ----------------------------
extract_roff() {	# man page: literal lines start with "\&"
	awk -v F="$1" 'substr($0,1,2)=="\\&" {
		t=substr($0,3)
		gsub(/\\-/,"-",t); gsub(/\\er/,"\\r",t); gsub(/\\en/,"\\n",t)
		gsub(/\\e/,"\\",t); gsub(/\\f[BIRP]/,"",t)
		print F "\t" NR "\t" t
	}' "$1" >> "$stream"
}
extract_fenced() {	# markdown: inside ``` fences
	awk -v F="$1" '
		/^```[a-z]/ { skip=1; next }		# ```mermaid and friends: not config
		/^```/      { if (skip) { skip=0; next } inb = !inb; next }
		inb && $0 !~ /…|::=/ { print F "\t" NR "\t" $0 }
	' "$1" >> "$stream"
}
extract_heredoc() {	# tests: inside the protocols.cfg heredoc
	awk -v F="$1" '/protocols\.cfg" <<'"'"'?CFG/ { inb=1; next } inb && /^CFG$/ { inb=0; next }
		       inb { print F "\t" NR "\t" $0 }' "$1" >> "$stream"
}
extract_plain() { awk -v F="$1" '{ print F "\t" NR "\t" $0 }' "$1" >> "$stream"; }

extract_plain   xymonnet/protocols.cfg
extract_roff    xymonnet/protocols.cfg.5
[ -f docs/design/protocol-dialogues.md ] && extract_fenced docs/design/protocol-dialogues.md
for t in tests/network/dialogue-*.sh tests/network/protocol-dialogue.sh; do
	[ -f "$t" ] && extract_heredoc "$t"
done
[ -s "$stream" ] || fail "no protocols.cfg text was found to check -- the extractors match nothing, so this suite would pass on anything"

# --- apply the rules ---------------------------------------------------------
awk -F'\t' '
function flush_state(   i) {
	# R3/R5 are per state, and are decided when the state ends
	if (statename != "" && actions > 1 && !permissive)
		bad[++n] = sprintf("%s:%d  R3  state \"%s\" holds %d actions; a state does one thing and waits for one answer",
				   sfile, sline, statename, actions)
	if (statename != "" && clock_after_expect && !permissive)
		bad[++n] = sprintf("%s:%d  R5  state \"%s\" writes a clock below its expects, where it bounds nothing",
				   sfile, sline, statename)
	statename=""; actions=0; expects=0; clock_after_expect=0
}
{
	file=$1; lineno=$2; raw=$3
	sub(/[[:space:]]+$/,"",raw)
	text=raw; sub(/^[[:space:]]+/,"",text)
	indent=length(raw)-length(text)

	if (text ~ /^#/) { if (text ~ /conventions:[[:space:]]*permissive/) permissive=1; next }
	if (text == "") next

	if (text ~ /^\[[^]]+\]$/) {			# a new entry
		flush_state()
		entry=text; efile=file; seen_state=0; permissive=0
		# "[service|alias]" heads the grammar summary, not an entry
		if (entry ~ /service\|alias/) entry=""
		next
	}
	if (entry == "") next

	# A block of literal text in a manual is not always an entry: the
	# DIAGNOSTICS table is set the same way. Anything that is not a
	# directive ends the entry rather than being judged as part of it.
	if (text !~ /^(state|send|expect|starttls|credentials|options|port|transport|start|always|eof|else)([[:space:]]|$)/ &&
	    text !~ /^(timeout|idle)\(/ &&
	    text !~ /^[A-Za-z_][A-Za-z0-9_-]*[[:space:]]+~[[:space:]]/) {
		flush_state(); entry=""; next
	}

	# R6 -- syntax that was tried and dropped
	if (text ~ /^(when|end|capture|capture-regex|expect-regex|expect-capture|goto)([[:space:]]|$)/ ||
	    text ~ /->[[:space:]]*green([[:space:]]|$)/)
		bad[++n] = sprintf("%s:%d  R6  %s uses a directive that was tried and dropped: %s", file, lineno, entry, text)

	if (text ~ /^state[[:space:]]/) {
		flush_state()
		statename=text; sub(/^state[[:space:]]+/,"",statename)
		sfile=file; sline=lineno; sindent=indent; seen_state=1
		next
	}

	if (text ~ /^(options|port|transport|start|framing)[[:space:]]/) {
		if (seen_state && !permissive)
			bad[++n] = sprintf("%s:%d  R1  %s writes \"%s\" after a state; attributes describe the definition and come first",
					   file, lineno, entry, text)
		next
	}

	if (!seen_state) next				# classic single-shot entry: R2-R5 do not apply

	if (indent <= sindent && !permissive)
		bad[++n] = sprintf("%s:%d  R2  %s: \"%s\" is not indented under state \"%s\"",
				   file, lineno, entry, text, statename)

	if (text ~ /^(send|starttls|credentials)([[:space:]]|$)/) actions++
	else if (text ~ /^(timeout|idle)\(/) { if (expects > 0) clock_after_expect=1 }
	else if (text ~ /^expect[[:space:]]/) {
		expects++
		if (text !~ /->[[:space:]]*[A-Za-z_]/ && !permissive)
			bad[++n] = sprintf("%s:%d  R4  %s: \"%s\" names no state to move to",
					   file, lineno, entry, text)
	}
}
END {
	flush_state()
	for (i=1; i<=n; i++) print bad[i]
	exit (n > 0)
}' "$stream" > "$work/problems.txt" || :

if [ -s "$work/problems.txt" ]; then
	fail "protocols.cfg entries break the conventions protocols.cfg(5) states under CONVENTIONS.
The parser accepts all of these; a reader cannot. Either write the entry the
documented way, or mark it '# conventions: permissive' if it exists to cover a
spelling the parser allows:

$(cat "$work/problems.txt")"
fi

pass "every published and tested dialogue entry follows the conventions the manual states"
