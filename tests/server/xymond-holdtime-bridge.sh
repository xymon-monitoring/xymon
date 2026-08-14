#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/xymond-holdtime-bridge.sh
#
# "lastchange" is the hold-time of the recorded colour -- how long the test has
# been this colour -- and alerts.cfg DURATION rules read it, through
# xymond_alert's eventstart. When a test stops reporting, xymond invents a
# status for it; if the test comes back wearing the colour it left with, the
# problem did not go away while we were blind, so the hold-time carries across.
#
# Two bounds keep that inference honest, and this drives both: the colour must
# match, and the silence must be short relative to how often the test reports.
# The second one matters because nothing caps how long a status may sit stale --
# check_purple_status() only ever deletes summaries -- so without a limit a host
# absent for a month would come back claiming a month-long hold-time and page at
# full DURATION escalation the moment it reappeared.
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

limit=$(sed -n 's/^#define GAPBRIDGE_VALIDITIES *\([0-9][0-9]*\).*/\1/p' "$XYMOND_C")
[ -n "$limit" ] || fail "could not read GAPBRIDGE_VALIDITIES from xymond/xymond.c"

# Both halves of the rule: holdtime_bridges() decides the colour and defers the
# length to gap_within_window(), which save_checkpoint() also asks on its own.
window=$(awk '/^static int gap_within_window/,/^}/' "$XYMOND_C")
[ -n "$window" ] || fail "could not locate gap_within_window() in xymond/xymond.c"

body=$(awk '/^static int holdtime_bridges/,/^}/' "$XYMOND_C")
[ -n "$body" ] || fail "could not locate holdtime_bridges() in xymond/xymond.c"

require_cc
[ -f "$ROOT/include/config.h" ] && [ -f "$ROOT/lib/libxymoncomm.a" ] \
	|| skip "tree not built (run make first; the post-build CI suite covers this)"

work=$(mktempdir)

# The configured link flags rather than a hand-picked SSLLIBS: they carry the
# library search path and the runtime path as well. Without the rpath the
# harness links on NetBSD and then cannot run -- "Shared object
# libpcre2-8.so.0 not found", since pkgsrc puts it under /usr/pkg/lib.
harness_ldflags=$(xymon_ldflags "$ROOT")

{
	printf '#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n'
	printf '#include "libxymon.h"\n'
	printf '#define NO_COLOR (COL_COUNT)\n'
	printf '#define GAPBRIDGE_VALIDITIES %s\n' "$limit"
	printf '%s\n' "$window"
	printf '%s\n' "$body"
	cat "$here/xymond-holdtime-bridge-harness.c"
} > "$work/harness.c"

# The configured compile flags, not just the in-tree include dirs: on the
# BSDs PCRE2 lives under /usr/local/include or /usr/pkg/include, and
# libxymon.h pulls in pcre2.h, so a hand-rolled -I list fails to compile
# there on a tree that builds perfectly well. The Makefiles already carry
# those paths.
harness_cflags=$(xymon_cflags "$ROOT")
# shellcheck disable=SC2086
"$CC" $harness_cflags -I"$ROOT/lib" -o "$work/harness" \
	"$work/harness.c" "$ROOT/lib/libxymoncomm.a" \
	$harness_ldflags 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; fail "harness does not compile"; }

"$work/harness" 2>"$work/stderr.log" \
	|| fail "hold-time bridge assertions failed:
$(cat "$work/stderr.log")"

pass "hold-time bridges a short same-colour silence, and only that"
