#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/web/reportlog-text-event-lines.sh
#
# The text availability report (generate_replog(), lib/reportlog.c) lists
# one event per line. The newline came after the cause, inside the code
# that prints it, so an event without a cause did not end its line and the
# next event ran onto it. Here the first of two events has no cause.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)

require_c_buildenv "$ROOT"

work=$(mktempdir)

pcre_libs=${PCRELIBS:-}
[ -n "$pcre_libs" ] || pcre_libs=$(sed -n 's/^PCRELIBS *= *//p' "$ROOT/Makefile")
harness_cflags=$(xymon_cflags "$ROOT")
harness_ldflags=$(xymon_ldflags "$ROOT")

# shellcheck disable=SC2086  # flag lists are meant to split
"$CC" $harness_cflags -o "$work/harness" \
	"$ROOT/tests/web/reportlog-text-harness.c" \
	"$ROOT/lib/libxymon.a" "$ROOT/lib/libxymoncomm.a" "$ROOT/lib/libxymon.a" \
	$pcre_libs $harness_ldflags 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; fail "the reportlog harness does not build"; }

# generate_replog() frames the HTML with the replog header and footer
# templates; empty ones will do. UTC keeps the event times fixed.
mkdir -p "$work/web"
: >"$work/web/replog_header"
: >"$work/web/replog_footer"

TZ=UTC XYMONHOME="$work" "$work/harness" "$work/report.txt" >"$work/report.html" 2>"$work/stderr.log" \
	|| fail "the reportlog harness failed: $(cat "$work/stderr.log")"

events=$(grep -c -E '^(Mon|Tue|Wed|Thu|Fri|Sat|Sun) ' "$work/report.txt" || true)
[ "$events" = 2 ] || fail "the text report has $events event lines, not 2 (the first event has no cause):
$(cat "$work/report.txt")"

second=$(grep -E '^(Mon|Tue|Wed|Thu|Fri|Sat|Sun) ' "$work/report.txt" | sed -n 2p)
assert_contains "Server timeout" "$second" "the second event's line carries its own cause"

pass "the text report ends every event's line, with or without a cause"
