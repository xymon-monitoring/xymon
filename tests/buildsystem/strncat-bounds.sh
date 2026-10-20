#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/buildsystem/strncat-bounds.sh
#
# A ratchet on one defect: strncat() with a size argument that does not leave
# room for the terminator.
#
# strncat(dst, src, n) appends up to n characters and then a NUL. A bound of
# the destination's whole remaining size therefore writes that NUL one byte
# past the end; the form that fits subtracts one.
#
#	strncat(dst, src, sizeof(dst) - strlen(dst));		/* one byte over */
#	strncat(dst, src, sizeof(dst) - strlen(dst) - 1);	/* fits */
#
# The tree has 88 of the first form. This test does not demand they be fixed:
# it records the count per file and fails when a file gains one, so a new call
# cannot join them unnoticed. Three open pull requests move such a line and
# keep its bound; one adds a new one -- that is what this catches, at review
# time rather than after.
#
# Why a test and not -Werror: the compilers only see part of it. clang's
# -Wstrncat-size fires on 43 of the 88, where the bound is the destination's
# own sizeof; a bound written as a literal or a macro -- (100 - strlen(dur)),
# (MAX_LINE_LEN - strlen(cause)) -- is the same mistake and no compiler
# reports it. gcc reports none of them, and sees the strncpy half instead
# (-Wstringop-truncation). A source rule covers both halves on every platform.
#
# A finding is a candidate, not a proof: a bound smaller than the destination
# is safe, so lowering the baseline wants reading the site, not a sweep.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
# Beside this script, reached through $0: a path spelled "$ROOT/...awk" reads as
# an in-tree archive to the link-flags rule of
# tests/buildsystem/test-suite-portability.sh, whose pattern ends [.]a.
here=$(dirname "$0")
detector="$here/strncat-bounds.awk"
baseline="$here/strncat-bounds.baseline"
[ -f "$detector" ] || skip "no detector in this checkout"
[ -f "$baseline" ] || skip "no baseline in this checkout"

work=$(mktempdir)

# --- the detector reports the bad form, and only it -------------------------
# A fixture rather than the tree: this is what keeps the rule honest, the way
# tests/buildsystem/test-suite-portability.sh probes its own rules.
cat >"$work/fixture.c" <<'EOF'
void f(char *src) {
	char dst[64];
	strncat(dst, src, sizeof(dst) - strlen(dst) - 1);	/* fits */
	strncat(dst, src, sizeof(dst) - strlen(dst));		/* over */
	strncat(dst, src,
		sizeof(dst) - strlen(dst));			/* over, split */
	strncat(dst, src, (100 - strlen(dst)));			/* over, macro-ish */
	xstrncat(dst, src, sizeof(dst));			/* not strncat */
}
EOF
awk -v FILE="fixture.c" -f "$detector" "$work/fixture.c" >"$work/probe" || true
assert_equal "3" "$(wc -l <"$work/probe" | tr -d " ")" \
	"the detector no longer reports exactly the three unbounded forms in the fixture (and not the correct one, nor xstrncat)"

# --- the tree, counted per file ---------------------------------------------
: >"$work/found"
for f in $(cd "$ROOT" && git ls-files '*.c'); do
	awk -v FILE="$f" -f "$detector" "$ROOT/$f" >>"$work/found" || true
done
awk -F: '{ print $1 }' "$work/found" | sort | uniq -c \
	| awk '{ print $1, $2 }' | sort -k2 >"$work/current"
sort -k2 "$baseline" >"$work/expected"

# --- compare, and report what grew -----------------------------------------
: >"$work/grown"
while read -r count path; do
	was=$(awk -v p="$path" '$2 == p { print $1 }' "$work/expected")
	[ -n "$was" ] || was=0
	if [ "$count" -gt "$was" ]; then
		printf '%s: %s now, %s in the baseline\n' "$path" "$count" "$was" >>"$work/grown"
	fi
done <"$work/current"

if [ -s "$work/grown" ]; then
	printf '%s\n' "a strncat() bound that leaves no room for the terminator was added:" >&2
	cat "$work/grown" >&2
	printf '%s\n' "fix the bound (subtract one), or explain the site and update $baseline" >&2
	fail "the strncat baseline grew"
fi

total=$(wc -l <"$work/found" | tr -d " ")
expected_total=$(awk '{ s += $1 } END { print s + 0 }' "$work/expected")
if [ "$total" -lt "$expected_total" ]; then
	pass_partial "$total sites, down from $expected_total: lower the baseline in $baseline"
fi

pass "no strncat() bound lost its room for the terminator ($total sites, baseline $expected_total)"
