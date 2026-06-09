#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/web/svcstatus-trends-overflow.sh
#
# Regression guard for the trends-page buffer overflow fixed by
# xymon-monitoring/xymon#38.
#
# generate_trends() appends each graph's <rrdlink> into a growable buffer
# (allrrdlinks). The bug had two parts in web/svcstatus-trends.c:
#   1. the size check used '>=' and ignored the NUL terminator, so the buffer
#      could be left exactly at capacity, and
#   2. the snprintf() return value was added straight to the write pointer.
#      Per C99 snprintf returns what WOULD have been written, so on truncation
#      the pointer advanced past the end of the buffer -- silently dropping all
#      subsequent graphs (worst on hosts with many filesystems).
#
# A behavioural test would need the whole web CGI plus a Xymon/RRD environment
# to reproduce, and .github build coverage already compiles the file; what we
# guard here is that the *fix* survives future edits -- the dangerous
# advance-by-snprintf pattern stays gone and the truncation handling stays
# present. Skips cleanly if the file is absent (e.g. after a CMake/web rework).

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
FILE="$ROOT/web/svcstatus-trends.c"

[ -f "$FILE" ] || skip "web/svcstatus-trends.c absent"
src=$(cat "$FILE")

# (1) The vulnerable pattern -- advancing the write pointer by snprintf()'s
# return value -- must be gone.
assert_not_contains "allrrdlinksend += snprintf(" "$src" \
	"trends overflow regressed: write pointer advanced by raw snprintf() return (#38)"

# (2) The size check must account for the NUL terminator (NEEDED+1 vs buflen),
# not the old '>=' against the bare length.
assert_contains "onelinklen + 1) > allrrdlinks_buflen" "$src" \
	"trends buffer size check lost its NUL-terminator headroom (#38)"

# (3) snprintf truncation/error must be detected and the pointer advanced only
# by bytes actually written.
assert_contains "(size_t)written >= available" "$src" \
	"trends snprintf truncation check missing (#38)"
assert_contains "allrrdlinksend += written;" "$src" \
	"trends no longer advances by bytes actually written (#38)"

pass "svcstatus-trends.c keeps the #38 buffer-overflow guards"
