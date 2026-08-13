#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/xymond-gap-validity.sh
#
# The window a no-data gap may be bridged across is GAPBRIDGE_VALIDITIES times
# the test's report validity. Which validity, though, was decided by whichever
# report happened to end the silence: handle_status() replaced log->validity
# with the returning report's before the gap was evaluated.
#
# So a test reporting every five minutes that fell silent for an hour and came
# back announcing "status+240" was judged against a sixteen-hour window and
# bridged an hour it had no business bridging; the reverse -- silent while
# promising four hours, back promising five minutes -- had a gap rejected that
# should have carried across. Neither is the reporter's doing: the promise that
# matters is the one that applied when the silence began.
#
# update_gapstate() is static and xymond has no library form, so the real one
# is extracted from xymond.c together with the two functions it defers to, and
# driven with a clock of the test's own. Reaching it through a live daemon
# means waiting out a report validity, which a test cannot do.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
here=$(dirname "$0")
XYMOND_C="$ROOT/xymond/xymond.c"
[ -f "$XYMOND_C" ] || skip "xymond/xymond.c not present in this checkout"

limit=$(sed -n 's/^#define GAPBRIDGE_VALIDITIES *\([0-9][0-9]*\).*/\1/p' "$XYMOND_C")
[ -n "$limit" ] || fail "could not read GAPBRIDGE_VALIDITIES from xymond/xymond.c"

window=$(awk '/^static int gap_within_window/,/^}/'   "$XYMOND_C")
bridge=$(awk '/^static int holdtime_bridges/,/^}/'    "$XYMOND_C")
gapst=$(awk  '/^static void update_gapstate/,/^}/'    "$XYMOND_C")
[ -n "$window" ] || fail "could not locate gap_within_window() in xymond/xymond.c"
[ -n "$bridge" ] || fail "could not locate holdtime_bridges() in xymond/xymond.c"
[ -n "$gapst" ]  || fail "could not locate update_gapstate() in xymond/xymond.c"

require_cc
[ -f "$ROOT/include/config.h" ] && [ -f "$ROOT/lib/libxymoncomm.a" ] \
	|| skip "tree not built (run make first; the post-build CI suite covers this)"

work=$(mktempdir)
ssllibs=$(sed -n 's/^SSLLIBS *= *//p' "$ROOT/Makefile")

# Only the fields the extracted code touches. The real xymond_log_t carries
# forty more that none of it reads.
{
	printf '#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n'
	printf '#include "libxymon.h"\n'
	printf '#define NO_COLOR (COL_COUNT)\n'
	printf '#define GAPBRIDGE_VALIDITIES %s\n' "$limit"
	printf 'typedef struct xymond_log_t {\n'
	printf '\tint oldcolor;\n\tint validity;\n\ttime_t lastchange;\n'
	printf '\tint pregapcolor;\n\tint pregapvalidity;\n'
	printf '\ttime_t pregaplastchange;\n\ttime_t gapstart;\n'
	printf '} xymond_log_t;\n'
	printf '%s\n' "$window"
	printf '%s\n' "$bridge"
	printf '%s\n' "$gapst"
	cat "$here/xymond-gap-validity-harness.c"
} > "$work/harness.c"

# The configured compile flags, not a hand-rolled -I list: libxymon.h pulls in
# pcre2.h, which lives under /usr/local/include or /usr/pkg/include on the BSDs.
harness_cflags=$(xymon_cflags "$ROOT")
# shellcheck disable=SC2086
"$CC" $harness_cflags -I"$ROOT/lib" -o "$work/harness" \
	"$work/harness.c" "$ROOT/lib/libxymoncomm.a" $ssllibs 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; fail "harness does not compile"; }

"$work/harness" >"$work/out.log" 2>&1 \
	|| fail "gap-validity assertions failed:
$(cat "$work/out.log")"

pass "a gap is judged against the validity that applied when it opened, not the returning report's"
