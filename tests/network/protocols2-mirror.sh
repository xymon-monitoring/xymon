#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/network/protocols2-mirror.sh
#
# protocols2.cfg says the same thing as protocols.cfg, in a form that can say
# more.
#
# The two files ship together and protocols2.cfg wins for every service it
# defines, so the state machine form is what actually runs. It was written by
# working through protocols.cfg entry by entry, and that is precisely the kind
# of work that loses a step quietly: [oratns] and [ircd] each end with an
# `expect ""` that asks the server to say SOMETHING, and both were dropped in
# the first pass. Nothing failed. The entries still connected, still sent, and
# still reported green -- against a peer that never answered at all.
#
# So the rule is checked rather than trusted: everything protocols.cfg does,
# protocols2.cfg must still do, in the same order. It may ADD -- the verdict
# alternatives that name what a 4xx means are the whole reason it exists -- but
# it may not drop a send, drop an expect, or reorder them.
#
# The comparison runs on what the PARSER built, not on what the files say. A
# step the file declares and the parser drops reads identically in the text,
# and that is the failure being watched for.
#
# LAYER: both shipped configurations, parsed. No sockets, no peers.

set -eu
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
ROOT=$(find_root)

require_cc
work=$(mktempdir); register_cleanup "rm -rf '$work'"

[ -f "$ROOT/include/config.h" ] || skip "tree is not configured"

cfg1="$ROOT/xymonnet/protocols.cfg"
cfg2="$ROOT/xymonnet/protocols2.cfg"
[ -r "$cfg1" ] || fail "protocols.cfg is missing from the tree"
[ -r "$cfg2" ] || fail "protocols2.cfg is missing from the tree"

build_xymon_libs "$ROOT" "$work/libbuild.log" libxymoncomm.a
harness_cflags=$(xymon_cflags "$ROOT")
harness_ldflags=$(xymon_ldflags "$ROOT")
pcre_libs=$(sed -n 's/^PCRELIBS *= *//p' "$ROOT/Makefile")
[ -n "$pcre_libs" ] || pcre_libs="-lpcre2-8"

# The dump harness links against the parser, so it reports the steps that were
# actually built rather than the lines that were written.
# shellcheck disable=SC2086
"$CC" $harness_cflags -o "$work/dump" \
	"$(dirname "$0")/protocols2-dump.c" \
	"$ROOT/lib/libxymoncomm.a" $pcre_libs $harness_ldflags 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; fail "the dump harness does not compile"; }

# Every entry's first alias: find_tcp_service takes one name, and the aliases
# of an entry share its steps.
names=$(sed -n 's/^\[\([^]|]*\).*\]$/\1/p' "$cfg1" | LC_ALL=C sort -u)
[ -n "$names" ] || fail "no service names could be read out of protocols.cfg"

# One tree with the classic file alone -- the conversation as it has always
# been. One with both, which is what an installed Xymon has, and where
# protocols2.cfg takes precedence.
mkdir -p "$work/classic/etc" "$work/shipped/etc"
cp "$cfg1" "$work/classic/etc/protocols.cfg"
cp "$cfg1" "$work/shipped/etc/protocols.cfg"
cp "$cfg2" "$work/shipped/etc/protocols2.cfg"

# shellcheck disable=SC2086
XYMONHOME="$work/classic" "$work/dump" $names > "$work/classic.steps" 2>"$work/classic.err" \
	|| { cat "$work/classic.err" >&2; fail "protocols.cfg alone did not parse"; }
# shellcheck disable=SC2086
XYMONHOME="$work/shipped" "$work/dump" $names > "$work/shipped.steps" 2>"$work/shipped.err" \
	|| { cat "$work/shipped.err" >&2; fail "the two files together did not parse:
$(cat "$work/shipped.err")"; }

# Two entries do not merely add; both replace an expect that took ANY reply with
# one that reads it, which is narrower rather than different. They are named
# here so the rule stays strict for the other thirty-five.
#
#   ircd    ":" -- every server line begins with the prefix colon
#   oratns  the TNS packet type at byte 4, which says whether this is a
#           listener at all
narrowed="ircd oratns"

missing=""
for svc in $names; do
	case " $narrowed " in *" $svc "*) continue ;; esac

	grep "^$svc	" "$work/classic.steps" > "$work/a" 2>/dev/null || : > "$work/a"
	grep "^$svc	" "$work/shipped.steps" > "$work/b" 2>/dev/null || : > "$work/b"

	# Is a a subsequence of b?
	if ! awk '
		NR==FNR { a[++n]=$0; next }
		        { b[++m]=$0 }
		END {
			i=1
			for (j=1; (j<=m) && (i<=n); j++) if (b[j] == a[i]) i++
			if (i<=n) { print a[i]; exit 1 }
			exit 0
		}' "$work/a" "$work/b" > "$work/lost"
	then
		missing="$missing
  $svc lost: $(cat "$work/lost")"
	fi
done

[ -z "$missing" ] || fail "protocols2.cfg drops steps that protocols.cfg performs.
Each line below is a step the classic entry runs and the shipped state machine
does not, so the service is checked less thoroughly than before:$missing"

# The other direction is not an error -- adding is the point -- but if NOTHING
# was ever added, protocols2.cfg is a copy and the state machine buys nothing.
classic_lines=$(wc -l < "$work/classic.steps")
shipped_lines=$(wc -l < "$work/shipped.steps")
[ "$shipped_lines" -gt "$classic_lines" ] || fail \
"protocols2.cfg performs no more steps than protocols.cfg ($shipped_lines vs
$classic_lines). Either it is not being loaded, or it is a transcription with
none of the alternatives that justify it."

pass "protocols2.cfg keeps every step protocols.cfg performs ($(echo "$names" | wc -w) services)"
