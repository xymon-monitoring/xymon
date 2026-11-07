#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/web/reportlog-text-sla-seconds.sh
#
# The text availability report (generate_replog(), lib/reportlog.c) ends
# with each total as a duration and its seconds: "0:30:00   (1800 secs)".
# For "Time Non-Critical (SLA)" the duration came from the SLA hours but
# the seconds from the 24x7 total, so a report with an SLA put two
# different numbers on one line. Here the non-critical time is 7200 s
# 24x7 and 3600 s within the SLA hours.

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

sla=$(grep -F 'Time Non-Critical (SLA)' "$work/report.txt" || true)
full=$(grep -F 'Time Non-Critical (24x7)' "$work/report.txt" || true)
[ -n "$sla" ] || fail "no 'Time Non-Critical (SLA)' line in the text report:
$(cat "$work/report.txt")"

assert_contains "(7200 secs)" "$full" "the 24x7 non-critical total is 7200 seconds"
assert_contains "(3600 secs)" "$sla" "the SLA non-critical total gives the SLA seconds, not the 24x7 ones"

pass "the text report's non-critical SLA total gives the SLA seconds"
