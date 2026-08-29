#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/analysis-pattern-error-context.sh
#
# Regression guard for the diagnostics emitted when a %pattern in analysis.cfg
# fails to compile (#253).
#
# Two things are pinned here.
#
# 1. Location. The pcre error alone printed only the (possibly truncated)
#    pattern, so an operator saw the same context-free line in clientdata.log,
#    rrd-data.log and rrd-status.log with nothing saying which config line
#    produced it. The parser now adds "at line N", following the convention
#    already used by the neighbouring syntax errors.
#
# 2. The whitespace hint, and its gate. analysis.cfg is tokenized on
#    whitespace, so a pattern written with a literal space is silently cut at
#    the space; when the cut lands inside a group or a character class the
#    remnant fails to compile. That specific cause gets a hint pointing at
#    [[:space:]]. The gate is pcre2's own error code -- MISSING_CLOSING_
#    PARENTHESIS / MISSING_SQUARE_BRACKET -- not a scan of the pattern text,
#    because counting delimiters cannot tell a group from a class literal.
#
# The fixture is four PROC rules, one per line, so the asserted line numbers
# are also a check that the parser reports the right one:
#
#   line 1  %(Synchronization Failed        truncated at the space -> hint
#   line 2  %bad{2,1}proc                   invalid on its own merits -> no hint
#   line 3  %[(]{2,1}                       '(' is a class literal, and the
#                                           failure is the quantifier -> no hint
#   line 4  %Synchronization[[:space:]]...  the supported spelling -> silent
#
# Line 3 is the case a delimiter-counting scanner gets wrong: it sees one
# unmatched '(' and offers the whitespace hint for a pattern that contains no
# whitespace at all. Line 4 keeps the whole thing honest -- without it, a
# parser that rejected every pattern would still pass lines 1-3.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

# Drives the built server-side client-data processor. Skips if unbuilt; a
# CMake/autopkgtest caller that exports XYMOND_CLIENT to a real path makes a
# dangling path a failure rather than a skip (see require_bin).
require_bin XYMOND_CLIENT xymond/xymond_client

work=$(mktempdir)

# No comment or blank lines: the parser counts every physical line, so the
# fixture's own numbering is what the assertions below expect.
cat >"$work/analysis.cfg" <<'EOF'
PROC %(Synchronization Failed 1 99 red
PROC %bad{2,1}proc 1 99 red
PROC %[(]{2,1} 1 99 red
PROC %Synchronization[[:space:]]Failed 1 99 red
EOF

# Diagnostics go to stderr (errprintf); the dump itself goes to stdout and is
# not what this test is about.
"$XYMOND_CLIENT" --config="$work/analysis.cfg" --dump-config >/dev/null 2>"$work/err" \
	|| fail "xymond_client --dump-config exited non-zero: $(cat "$work/err")"

errs=$(cat "$work/err")

trunc_line=$(printf '%s\n' "$errs" | grep -F "Invalid pattern '%(Synchronization'") || true
quant_line=$(printf '%s\n' "$errs" | grep -F "Invalid pattern '%bad{2,1}proc'")     || true
class_line=$(printf '%s\n' "$errs" | grep -F "Invalid pattern '%[(]{2,1}'")         || true

[ -n "$trunc_line" ] || fail "no diagnostic for the space-truncated pattern: $errs"
[ -n "$quant_line" ] || fail "no diagnostic for the invalid quantifier pattern: $errs"
[ -n "$class_line" ] || fail "no diagnostic for the class-literal pattern: $errs"

# (1) Every diagnostic names the config line it came from -- the point of #253.
assert_contains "at line 1" "$trunc_line" \
	"invalid pattern on line 1 is not reported with its config line"
assert_contains "at line 2" "$quant_line" \
	"invalid pattern on line 2 is not reported with its config line"
assert_contains "at line 3" "$class_line" \
	"invalid pattern on line 3 is not reported with its config line"


# Whether this build could name the two pcre2 error codes the hint is gated on.
# Both are absent from the pcre2 headers on some older distributions, where the
# hint is compiled out on purpose -- asserting it there would be failing over a
# platform limit rather than a regression. Probed with the compiler when there
# is one; where there is not (an installed-package run), the names are assumed
# present, which is the case on every platform that ships a current pcre2.
hint_available=yes
if command -v "${CC:-cc}" >/dev/null 2>&1; then
	ROOT=$(find_root)
	printf '#define PCRE2_CODE_UNIT_WIDTH 8\n#include <pcre2.h>\nint v = PCRE2_ERROR_MISSING_SQUARE_BRACKET + PCRE2_ERROR_MISSING_CLOSING_PARENTHESIS;\n' \
		>"$work/pcre2probe.c"
	# shellcheck disable=SC2046,SC2086
	"${CC:-cc}" $(pcre_cflags "$ROOT") -fsyntax-only "$work/pcre2probe.c" 2>/dev/null || hint_available=no
fi

# (2) The hint fires for a pattern actually truncated at a space.
if [ "$hint_available" = yes ]; then
	assert_contains "[[:space:]]" "$trunc_line" \
		"a pattern truncated at a space no longer suggests [[:space:]]"
else
	assert_not_contains "hint" "$trunc_line" \
		"this pcre2 cannot name the gate's error codes, so no hint may be offered at all"
fi

# (3) ...and only then. A pattern that is invalid for an unrelated reason gets
# the location and nothing else.
assert_not_contains "hint" "$quant_line" \
	"whitespace hint offered for '%bad{2,1}proc', which contains no whitespace"

# (4) The regression the delimiter-counting version had: '(' inside a class is
# a literal, and this pattern fails on its quantifier, not on an open group.
assert_not_contains "hint" "$class_line" \
	"whitespace hint offered for '%[(]{2,1}' -- '(' inside [] counted as an unclosed group"

# (5) The supported spelling compiles, so nothing at all is reported for it.
# This also proves (1)-(4) are not passing because every pattern is rejected.
assert_not_contains "Synchronization[[:space:]]Failed" "$errs" \
	"a valid [[:space:]] pattern produced a diagnostic"

count=$(printf '%s\n' "$errs" | grep -c "Invalid pattern" || true)
assert_equal "3" "$count" \
	"expected exactly 3 invalid-pattern diagnostics from a 4-line config, got $count"

pass "invalid analysis.cfg patterns report their config line, and the whitespace hint is gated on pcre2's error code (#253)"
