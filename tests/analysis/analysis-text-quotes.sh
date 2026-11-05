#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/analysis/analysis-text-quotes.sh
#
# A setting written KEYWORD="value with blanks" in analysis.cfg is read as
# "KEYWORD=value with blanks" is: without quotes (#624, Debian #579739).
#
# The tokenizer, wstok() in lib/misc.c, kept the quoted value together but
# stripped only a quote at the very start of the token and one at its end.
# PROC ... TEXT="some text" therefore came out as TEXT="some text, its
# opening quote left in, and the procs summary showed it.
#
# Checked through xymond_client --dump-config, which prints each rule as it
# was parsed: TEXT="..." and "TEXT=..." both give the bare text; an unquoted
# TEXT= still ends at the first blank, as analysis.cfg(5) says; and a pattern
# that has a quote after an '=', %key="value", is left as it was parsed
# before, since only a keyword's quotes go.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin XYMOND_CLIENT xymond/xymond_client

work=$(mktempdir)

cat >"$work/analysis.cfg" <<'EOF2'
PROC test1 0 1 yellow TEXT="text with double quotes"
PROC test2 0 2 yellow "TEXT=text with double quotes"
PROC test3 0 3 yellow TEXT=unquoted text
PROC %key="value" 0 4 yellow
EOF2

dump=$("$XYMOND_CLIENT" --config="$work/analysis.cfg" --dump-config 2>/dev/null) \
	|| fail "xymond_client --dump-config exited non-zero"

# rule NAME -- the dump line of the PROC rule for NAME
rule() { printf '%s\n' "$dump" | grep -F "PROC $1 " || true; }

assert_contains 'TEXT=text with double quotes (line: 1)' "$(rule test1)" \
	"TEXT=\"...\" kept a quote: the value must be the bare text"
assert_contains 'TEXT=text with double quotes (line: 2)' "$(rule test2)" \
	"\"TEXT=...\" no longer gives the bare text"
assert_contains 'TEXT=unquoted (line: 3)' "$(rule test3)" \
	"an unquoted TEXT= no longer ends at the first blank, as analysis.cfg(5) documents"
assert_contains 'PROC %key="value 0 4' "$(rule '%key="value')" \
	"a pattern with a quote after '=' was changed: only a keyword's quotes may go"

pass "KEYWORD=\"value\" and \"KEYWORD=value\" both give the bare value, an unquoted value ends at a blank, and a pattern is left alone"
