#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/alert-for-holdtime.sh
#
# Behavioural guard for the FOR=N alerts.cfg criterion (issue #229, step 3).
#
# FOR=N matches when the rule's colour has held for N minutes, where
# DURATION>N measures the age of the alert event and deliberately survives
# yellow->red. The pair is what makes "delay this colour transition"
# expressible, so the tests below check both halves of the distinction and
# not merely that FOR counts minutes:
#
#   - below and above the threshold, the obvious cases;
#   - a red that arrived after two hours of yellow: DURATION>10 matches it,
#     FOR=10 must not. A build reading eventstart where it should read
#     colorstart passes every other case and fails this one;
#   - rules FOR cannot mean: several colours, or none named at all.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
here=$(dirname "$0")

CC=${CC:-cc}
command -v "$CC" >/dev/null 2>&1 || skip "no C compiler available (CC=$CC)"
require_gnu_make

[ -f "$ROOT/include/config.h" ] && [ -f "$ROOT/lib/libxymoncomm.a" ] \
	|| skip "tree not built (run make first; the post-build CI suite covers this)"

work=$(mktempdir)
harness="$work/harness"

"$XYMON_MAKE" -C "$ROOT/lib" libxymoncomm.a >"$work/libbuild.log" 2>&1 \
	|| { cat "$work/libbuild.log" >&2; fail "cannot refresh libxymoncomm.a"; }

pcre_libs=${PCRELIBS:-}
if [ -z "$pcre_libs" ] && command -v pkg-config >/dev/null 2>&1; then
	pcre_libs=$(pkg-config --libs libpcre2-8 2>/dev/null || true)
fi
[ -n "$pcre_libs" ] || pcre_libs="-lpcre2-8"

# The configured flags, not a hand-picked SSLLIBS: xymon_ldflags() carries the
# library search path and the rpath, without which the harness links on NetBSD
# and then cannot run, pkgsrc putting libpcre2-8.so under /usr/pkg/lib. PCRE
# itself is not in there -- it is not one of libxymoncomm's own libraries --
# so it stays beside it, as the other alerts harnesses spell it.
harness_cflags=$(xymon_cflags "$ROOT")
harness_ldflags=$(xymon_ldflags "$ROOT")
# shellcheck disable=SC2086  # deliberate word-splitting, as the neighbouring tests do
"$CC" $harness_cflags -o "$harness" "$here/alert-for-holdtime-harness.c" \
	"$ROOT/lib/libxymoncomm.a" $harness_ldflags $pcre_libs 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; fail "harness does not compile"; }

cat >"$work/hosts.cfg" <<'EOF'
127.0.0.1  testhost  # conn
EOF

# run <alerts.cfg-body> <color> <colorage-sec> <eventage-sec> [report]
run() {
	printf '%s\n' "$1" >"$work/alerts.cfg"
	"$harness" "$work/hosts.cfg" "$work/alerts.cfg" "$2" "$3" "$4" "${5:-}" 2>"$work/run.err"
}

RULE_FOR10='HOST=testhost SERVICE=conn COLOR=red FOR=10
	MAIL admin@example.com'

# ---- the threshold itself ---------------------------------------------------

got=$(run "$RULE_FOR10" red 300 300)
assert_equal "NOMATCH anymatch=1" "$got" "red held 5 minutes must not satisfy FOR=10"

got=$(run "$RULE_FOR10" red 900 900)
assert_equal "MATCH anymatch=1" "$got" "red held 15 minutes must satisfy FOR=10"

# ---- FOR is not DURATION ----------------------------------------------------

# Two hours of yellow, then red 5 minutes ago. The event is old, the colour is
# young. This is the case the criterion exists for.
got=$(run "$RULE_FOR10" red 300 7200)
assert_equal "NOMATCH anymatch=1" "$got" \
	"a red 5 minutes old must not satisfy FOR=10 because its event is 2 hours old"

# A blocked FOR must mean "nobody is due yet", never "no rule covers this
# alert". xymond_alert (xymond_alert.c:941-948) reacts to the second by going
# A_NORECIP and calling cleanup_alert(), which discards the repeat state and
# with it the recovery message for an alert it already sent. anymatch is what
# separates the two, so every expectation above carries it -- evaluating FOR
# on the rule pass, where criteriamatch is called with anymatch=NULL
# (loadalerts.c:1147,1172), prints anymatch=0 and the assertions name it.

# FOR written on a recipient line, inheriting the rule's single COLOR=.
got=$(run 'HOST=testhost SERVICE=conn COLOR=red
	MAIL oncall@example.com FOR=10' red 300 300)
assert_equal "NOMATCH anymatch=1" "$got" \
	"a recipient-level FOR must defer without repeating COLOR= on its line"
assert_not_contains "Ignoring FOR" "$(cat "$work/run.err")" \
	"a recipient FOR must inherit the colour its rule already pins"

got=$(run 'HOST=testhost SERVICE=conn COLOR=red
	MAIL oncall@example.com FOR=10' red 900 900)
assert_equal "MATCH anymatch=1" "$got" \
	"the same recipient must be due once the colour has held long enough"

RULE_DUR10='HOST=testhost SERVICE=conn COLOR=red DURATION>10
	MAIL admin@example.com'
got=$(run "$RULE_DUR10" red 300 7200)
assert_equal "MATCH anymatch=1" "$got" \
	"the same alert must satisfy DURATION>10 -- otherwise the case proves nothing"

# ---- rules FOR cannot mean --------------------------------------------------

# Several colours: "the colour has held 10 minutes" has no single referent.
got=$(run 'HOST=testhost SERVICE=conn COLOR=red,yellow FOR=10
	MAIL admin@example.com' red 60 60)
assert_contains "needs a single COLOR=" "$(cat "$work/run.err")" \
	"a multi-colour FOR rule must be reported at load time"
assert_equal "MATCH anymatch=1" "$got" \
	"a rejected FOR must leave the rest of the rule matching, not silently drop it"

# No COLOR= at all: the effective set is the default one, never a single colour.
got=$(run 'HOST=testhost SERVICE=conn FOR=10
	MAIL admin@example.com' red 60 60)
assert_contains "needs an explicit single COLOR=" "$(cat "$work/run.err")" \
	"a FOR rule without COLOR= must be reported at load time"
assert_equal "MATCH anymatch=1" "$got" \
	"a rejected FOR must leave the rest of the rule matching here too"

# The colour a recipient can actually alert on is what the rule and the
# recipient BOTH name: criteriamatch() is run for each in turn. So COLOR= sets
# that overlap in exactly one colour leave FOR with a single referent, and must
# be accepted -- counting the recipient's own two colours rejected this and sent
# the alert immediately.
got=$(run 'HOST=testhost SERVICE=conn COLOR=red
	MAIL oncall@example.com COLOR=red,yellow FOR=10' red 300 300)
assert_not_contains "Ignoring FOR" "$(cat "$work/run.err")" \
	"overlapping COLOR= sets naming one colour between them must not reject FOR"
assert_equal "NOMATCH anymatch=1" "$got" \
	"and that FOR must actually defer: red held 5 minutes does not satisfy FOR=10"

got=$(run 'HOST=testhost SERVICE=conn COLOR=red
	MAIL oncall@example.com COLOR=red,yellow FOR=10' red 900 900)
assert_equal "MATCH anymatch=1" "$got" \
	"red held 15 minutes satisfies it, so the deferral is the threshold and not a block"

# No colour in common: the recipient can never fire, FOR or no FOR. Say that,
# rather than "it needs an explicit single COLOR=", which it has.
got=$(run 'HOST=testhost SERVICE=conn COLOR=red
	MAIL oncall@example.com COLOR=yellow FOR=10' red 900 900)
assert_contains "nothing in common with the rule" "$(cat "$work/run.err")" \
	"a recipient whose COLOR= cannot overlap its rule must be reported at load time"

# A unit we do not know is a typo, not a delimiter. The parser stops at the
# first character outside 0123456789mhdw, so "10x" used to read as ten minutes
# and load clean -- a hold time that is not what the line says.
got=$(run 'HOST=testhost SERVICE=conn COLOR=red FOR=10x
	MAIL admin@example.com' red 60 60)
assert_contains "Ignoring invalid FOR" "$(cat "$work/run.err")" \
	"a FOR with an unknown unit must be reported, not read as minutes"
assert_equal "MATCH anymatch=1" "$got" \
	"and the rest of the rule must keep matching"

# ---- a duration that does not fit ------------------------------------------
#
# A duration overflows in two places, and the interesting one is not the
# obvious one. durationvalue() accumulates minutes in an int, so "426089w" --
# well within what the syntax accepts -- wrapped to 9824 minutes and would
# have loaded clean as a hold time of under seven days: a config silently
# meaning something other than what it says. Multiplying wide at the call site
# cannot see that, because the wrap already happened inside.
#
# Both are closed by bounding the minutes, so what this case pins is the
# reported input, not just a large one (@SoundGoof).

got=$(run 'HOST=testhost SERVICE=conn COLOR=red FOR=426089w
	MAIL admin@example.com' red 300 300)
assert_contains "Ignoring invalid FOR" "$(cat "$work/run.err")" \
	"a FOR that does not fit must be reported at load time, not wrapped into a short one"
assert_equal "MATCH anymatch=1" "$got" \
	"a rejected FOR must leave the rest of the rule matching, as the other rejections do"

# The bound is on the minutes, so it lands one multiplication earlier than a
# reader expects: INT_MAX/60 minutes is the largest hold time there is. Both
# sides of it are pinned, or an off-by-one either way would go unnoticed.
got=$(run 'HOST=testhost SERVICE=conn COLOR=red FOR=35791394
	MAIL admin@example.com' red 300 300)
assert_not_contains "Ignoring invalid FOR" "$(cat "$work/run.err")" \
	"INT_MAX/60 minutes is representable and must be accepted"
assert_equal "NOMATCH anymatch=1" "$got" \
	"a hold time of INT_MAX/60 minutes must be applied, not ignored"

got=$(run 'HOST=testhost SERVICE=conn COLOR=red FOR=35791395
	MAIL admin@example.com' red 300 300)
assert_contains "Ignoring invalid FOR" "$(cat "$work/run.err")" \
	"one minute past INT_MAX/60 must be refused"

# ---- the rule sets the threshold, the recipient names the colour ------------
#
# The effective colour set is the rule's and the recipient's together, so a FOR
# on the rule line with "MAIL x COLOR=red" under it names exactly one colour at
# match time. Judged against the rule line alone it named none, the FOR was
# dropped, and the alert went out at once -- the opposite of what was asked.
RULE_SPLIT='HOST=testhost SERVICE=conn FOR=10
	MAIL admin@example.com COLOR=red'
got=$(run "$RULE_SPLIT" red 300 300)
assert_equal "NOMATCH anymatch=1" "$got" \
	"a FOR on the rule line was dropped because the colour is on the recipient"
assert_not_contains "Ignoring FOR" "$(cat "$work/run.err")" \
	"the rule's FOR was refused although the recipient names a single colour"
got=$(run "$RULE_SPLIT" red 900 900)
assert_equal "MATCH anymatch=1" "$got" \
	"the same rule must alert once the colour has held long enough"

# ---- a threshold on both lines takes the stricter one -----------------------
#
# Every other criterion is checked in both passes, so a rule and a recipient
# that both set one must both be satisfied. FOR is checked in the recipient
# pass alone, where taking the recipient's value made it the one criterion a
# recipient could loosen.
RULE_BOTH='HOST=testhost SERVICE=conn COLOR=red FOR=60
	MAIL admin@example.com FOR=10'
assert_equal "NOMATCH anymatch=1" "$(run "$RULE_BOTH" red 900 900)" \
	"a recipient loosened the rule's FOR instead of adding to it"
assert_equal "MATCH anymatch=1" "$(run "$RULE_BOTH" red 4200 4200)" \
	"the stricter of the two thresholds was never satisfied"

# And the config report must say the same thing the engine does. It computes
# the delay column a second time, on its own, so the two can disagree: taking
# the recipient's 10m there told an operator "first alert after 10 minutes"
# about an alert xymond_alert holds for an hour.
report=$(run "$RULE_BOTH" red 300 300 report)
assert_contains "center>1h <" "$report" \
	"the report shows the recipient's FOR where the engine applies the rule's"
assert_not_contains "center>10m <" "$report" \
	"the delay column names a threshold that will not be the one applied"

pass "FOR=N measures the hold time of the rule's colour, not the age of the event"
