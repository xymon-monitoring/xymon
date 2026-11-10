#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/web/reportlog-event-count.sh
#
# The HTML availability report (generate_replog(), lib/reportlog.c) has a
# column per color, green first, and every row fills it but one: the
# "Event count" row started at yellow and widened its label over the green
# column with COLSPAN=2, so the green count was never shown. The text
# report printed all six.
#
# Each color gets a different count, so a missing or shifted cell shows:
# the row must read its label, then the six counts in the column order.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)

require_c_buildenv "$ROOT"

work=$(mktempdir)

pcre_libs=${PCRELIBS:-}
[ -n "$pcre_libs" ] || pcre_libs=$(xymon_makevar "$ROOT" PCRELIBS)
harness_cflags=$(xymon_cflags "$ROOT")
harness_ldflags=$(xymon_ldflags "$ROOT")

# shellcheck disable=SC2086  # flag lists are meant to split
"$CC" $harness_cflags -o "$work/harness" \
	"$ROOT/tests/web/reportlog-event-count-harness.c" \
	"$(xymon_lib "$ROOT" libxymon.a)" "$(xymon_lib "$ROOT" libxymoncomm.a)" "$(xymon_lib "$ROOT" libxymon.a)" \
	$pcre_libs $harness_ldflags 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; fail "the reportlog harness does not build"; }

# generate_replog() frames the table with the replog header and footer
# templates; empty ones keep the output to the table.
mkdir -p "$work/web"
: >"$work/web/replog_header"
: >"$work/web/replog_footer"

XYMONHOME="$work" "$work/harness" >"$work/report.html" 2>"$work/stderr.log" \
	|| fail "the reportlog harness failed: $(cat "$work/stderr.log")"

# The cells of the row whose first cell is "Event count", tags removed,
# joined with "|".
row=$(awk '
	/<TR/ { n = 0 }
	/<TD/ { c = $0; gsub(/<[^>]*>/, "", c); cell[++n] = c }
	/<\/TR>/ && n && cell[1] == "Event count" {
		s = cell[1]
		for (i = 2; i <= n; i++) s = s "|" cell[i]
		print s
		exit
	}' "$work/report.html")

[ -n "$row" ] || fail "no Event count row in the report:
$(cat "$work/report.html")"
[ "$row" = "Event count|7|3|5|2|4|6" ] \
	|| fail "the Event count row reads '$row', not 'Event count|7|3|5|2|4|6' (green 7, yellow 3, red 5, purple 2, clear 4, blue 6)"

pass "the HTML availability report shows the event count of every color, green included, in its own column"
