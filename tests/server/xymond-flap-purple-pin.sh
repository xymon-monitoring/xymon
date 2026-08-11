#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/xymond-flap-purple-pin.sh
#
# While a test is flapping, xymond records the most critical level it reported
# rather than following every oscillation. Purple is not a level -- it means
# "no data" -- so it must stay out of that comparison on both sides.
#
# Exempting only the incoming color is not enough. Purple (3) outranks green
# (0), clear (1) and blue (2), so once a flapping test has been recorded purple
# (its client went quiet and check_purple_status() said so), the pin rewrites
# every subsequent report from the resumed client back to purple. The test then
# displays "no data" -- and pages for it, purple being an alert color by
# default -- while it is talking to us, until the flap window expires.
#
# This drives xymond's real flap_pinned_color() over every color pair.
#
# Source-only apart from libxymoncomm.a, which supplies nothing here but keeps
# the harness link identical to its siblings.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
here=$(dirname "$0")

XYMOND_C="$ROOT/xymond/xymond.c"
[ -f "$XYMOND_C" ] || skip "xymond/xymond.c not present in this checkout"

body=$(awk '/^static int flap_pinned_color/,/^}/' "$XYMOND_C")
[ -n "$body" ] || fail "could not locate flap_pinned_color() in xymond/xymond.c"

require_cc
[ -f "$ROOT/include/config.h" ] && [ -f "$ROOT/lib/libxymoncomm.a" ] \
	|| skip "tree not built (run make first; the post-build CI suite covers this)"

work=$(mktempdir)

ssllibs=$(sed -n 's/^SSLLIBS *= *//p' "$ROOT/Makefile")

{
	printf '#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n'
	printf '#include "libxymon.h"\n'
	printf '%s\n' "$body"
	cat "$here/xymond-flap-purple-pin-harness.c"
} > "$work/harness.c"

"$CC" -I"$ROOT/include" -I"$ROOT/lib" -o "$work/harness" \
	"$work/harness.c" "$ROOT/lib/libxymoncomm.a" \
	$ssllibs 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; fail "harness does not compile"; }

"$work/harness" 2>"$work/stderr.log" \
	|| fail "flap colour-pin assertions failed:
$(cat "$work/stderr.log")"

pass "a flapping test recorded purple keeps the colour it reports when it resumes"
