#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/alert-escalation-repeat.sh
#
# A severity increase must clear the repeat interval left by the colour it came
# from, even when the new colour carries FOR= and is not due to be sent yet.
#
# The two are separate questions that used to share one walk. FOR= holds an
# alert back from being SENT; clear_interval() asks who the alert could reach,
# so that a repeat record from the previous colour is forgotten. Both went
# through next_recipient(), so a red rule with FOR= returned nobody at the
# instant of escalation, the yellow's record survived, and the red was then
# dropped until that record came due -- delaying the very transition FOR=
# exists to damp.
#
# Repeat records are keyed hostname|testname|method|recipient (do_alert.c), not
# by rule, so one MAIL address named under both rules shares one record. That
# is what makes the two rules interact at all.

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

# -iquote xymond: the harness #includes do_alert.c for its static repeat list.
harness_cflags=$(xymon_cflags "$ROOT")
harness_ldflags=$(xymon_ldflags "$ROOT")
# shellcheck disable=SC2086  # deliberate word-splitting, as the neighbouring tests do
"$CC" $harness_cflags -iquote "$ROOT/xymond" -o "$harness" "$here/alert-escalation-repeat-harness.c" \
	"$ROOT/lib/libxymoncomm.a" $harness_ldflags $pcre_libs 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; fail "harness does not compile"; }

cat >"$work/hosts.cfg" <<'EOF'
127.0.0.1  testhost  # conn
EOF

# The shape that fails: one address, two rules, FOR= on the escalated one.
cat >"$work/alerts.cfg" <<'EOF'
HOST=testhost SERVICE=conn COLOR=yellow
	MAIL admin@example.com REPEAT=60
HOST=testhost SERVICE=conn COLOR=red FOR=10
	MAIL admin@example.com REPEAT=60
EOF

got=$("$harness" "$work/hosts.cfg" "$work/alerts.cfg" 3600 2>"$work/run.err") \
	|| { cat "$work/run.err" >&2; fail "harness did not run"; }
assert_equal "nextalert=cleared" "$got" \
	"escalating to a colour whose FOR= is not up yet must still clear the previous colour's repeat interval"

# Without FOR= the walk never had anything to skip: this is the control, and it
# fails if the harness stopped exercising clear_interval() at all.
cat >"$work/alerts-nofor.cfg" <<'EOF'
HOST=testhost SERVICE=conn COLOR=yellow
	MAIL admin@example.com REPEAT=60
HOST=testhost SERVICE=conn COLOR=red
	MAIL admin@example.com REPEAT=60
EOF

got=$("$harness" "$work/hosts.cfg" "$work/alerts-nofor.cfg" 3600 2>"$work/run.err") \
	|| { cat "$work/run.err" >&2; fail "harness did not run for the control case"; }
assert_equal "nextalert=cleared" "$got" \
	"the same escalation without FOR= must clear it too -- otherwise this test proves nothing"

pass "a severity increase clears the previous colour's repeat interval, FOR= or not"
