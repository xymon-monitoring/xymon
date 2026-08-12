#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/analysis-ds-firstmatch.sh
#
# Regression guard for analysis.cfg first-match semantics on DS (RRD dataset)
# threshold rules -- issue #32, fixed in commit 999c3ee03.
#
# analysis.cfg is documented as evaluated top-to-bottom, first match wins: put
# the specific settings first, the generic ones last. Every threshold type
# honoured that except DS: check_rrdds_thresholds() looped over *every* matching
# DS rule and emitted a "modify" for each, so a later wildcard rule was not
# shadowed by an earlier specific one -- both fired.
#
# The fix records each (column, dataset, colour) target the moment a rule
# applies to it -- i.e. as soon as the dataset is present and has a value, even
# if that rule's own threshold does not trigger -- and shadows later rules for
# the same target and colour. Different colours are deliberately kept, so a
# yellow and a red threshold on one dataset both still fire.
#
# check_rrdds_thresholds() is not exposed by any binary's CLI, so this compiles
# a small harness (analysis-ds-firstmatch-harness.c) against the real
# xymond/client_config.c and lib, loads real hosts.cfg/analysis.cfg fixtures,
# and counts "modify" lines per RRD key. Three scenarios, keyed by distinct RRD
# keys so they do not interfere:
#
#   k_shadow  : a specific rule that does NOT trigger, then a general rule of the
#               same colour that WOULD -> 0 modifies. This is the sharp edge of
#               the fix: the first matching rule claims the target and shadows
#               the later one even though it never raised a status itself.
#               Before the fix this scenario emitted 1.
#   k_colours : a yellow rule and a red rule on the same dataset, both
#               triggering -> 2 modifies. Guards the other side: the shadow must
#               be per-colour, so multi-level DS checks keep working. A fix that
#               shadowed across colours would drop this to 1.
#   k_first   : two same-colour rules where the FIRST triggers -> 1 modify. The
#               first-match result; before the fix this emitted 2.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
here=$(dirname "$0")

CLIENT_CONFIG_C="$ROOT/xymond/client_config.c"
[ -f "$CLIENT_CONFIG_C" ] || skip "xymond/client_config.c not present in this checkout"

CC=${CC:-cc}
command -v "$CC" >/dev/null 2>&1 || skip "no C compiler available (CC=$CC)"
require_gnu_make

[ -f "$ROOT/include/config.h" ] && [ -f "$ROOT/lib/libxymoncomm.a" ] \
	|| skip "tree not built (run make first; the post-build CI suite covers this)"

work=$(mktempdir)

"$XYMON_MAKE" -C "$ROOT/lib" libxymoncomm.a >"$work/libbuild.log" 2>&1 \
	|| { cat "$work/libbuild.log" >&2; fail "cannot refresh libxymoncomm.a"; }

ssllibs=$(sed -n 's/^SSLLIBS *= *//p' "$ROOT/Makefile")
pcre_libs=${PCRELIBS:-}
if [ -z "$pcre_libs" ] && command -v pkg-config >/dev/null 2>&1; then
	pcre_libs=$(pkg-config --libs libpcre2-8 2>/dev/null || true)
fi
[ -n "$pcre_libs" ] || pcre_libs="-lpcre2-8"

"$CC" -iquote "$ROOT/include" -iquote "$ROOT/lib" -iquote "$ROOT/xymond" -o "$work/harness" \
	"$here/analysis-ds-firstmatch-harness.c" "$CLIENT_CONFIG_C" \
	"$ROOT/lib/libxymoncomm.a" $ssllibs $pcre_libs 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; fail "harness does not compile"; }

cat >"$work/hosts.cfg" <<'EOF'
127.0.0.1 testhost #
EOF

# DS syntax: DS <column> <rrdkey>:<dataset> <threshold> color=<colour>
# Dataset is always "load"; the harness feeds the value 3.5.
cat >"$work/analysis.cfg" <<'EOF'
DS c_shadow  k_shadow:load  >10.0 color=red
DS c_shadow  k_shadow:load  >1.0  color=red
DS c_colours k_colours:load >1.0  color=yellow
DS c_colours k_colours:load >2.0  color=red
DS c_first   k_first:load   >1.0  color=red
DS c_first   k_first:load   >2.0  color=red
EOF

out=$("$work/harness" "$work/hosts.cfg" "$work/analysis.cfg" \
	load 3.5 k_shadow k_colours k_first 2>"$work/run.log") \
	|| { cat "$work/run.log" >&2; fail "harness run failed"; }

get() { printf '%s\n' "$out" | sed -n "s/^$1=//p"; }

assert_equal "0" "$(get k_shadow)" \
	"DS first-match regressed: a non-triggering specific rule no longer shadows a later same-colour rule (#32)"
assert_equal "2" "$(get k_colours)" \
	"DS shadowing is too aggressive: yellow and red thresholds on one dataset must both fire (#32)"
assert_equal "1" "$(get k_first)" \
	"DS first-match regressed: two same-colour rules both fired instead of the first winning (#32)"

pass "DS threshold rules honour analysis.cfg first-match per (column, dataset, colour) (#32)"
