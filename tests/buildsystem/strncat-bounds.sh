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
# The tree still has some of the first form. This test does not demand they be
# fixed: it records the count per file and fails when a file gains one, so a
# new call cannot join them unnoticed. It fails too when a file has fewer than
# its baseline: a fixed site left in the baseline is room for a new one to take
# its place unnoticed, which is how the count stood at 89 for 47 sites after
# the 42 fixed by #909. Whoever fixes one lowers the baseline in the same change. Three open pull requests move such a line and
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
# tests/buildsystem/test-suite-lint.sh, whose pattern ends [.]a.
here=$(dirname "$0")
detector="$here/strncat-bounds.awk"
baseline="$here/strncat-bounds.baseline"
[ -f "$detector" ] || skip "no detector in this checkout"
[ -f "$baseline" ] || skip "no baseline in this checkout"

work=$(mktempdir)

# --- the detector reports the bad form, and only it -------------------------
# A fixture rather than the tree: this is what keeps the rule honest, the way
# tests/buildsystem/test-suite-lint.sh probes its own rules.
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
# git ls-files where it works, find where it does not. git ls-files lists
# the tracked .c, and ignores an untracked scratch .c a developer dropped in
# the tree, which find would count. It is used only when $ROOT is the top of
# its own work tree: a release tarball unpacked inside another git checkout, or
# a lane container without git, falls back to find -- otherwise git ls-files
# would answer for the outer repo, or be empty, and every site would read as
# removed. find then lists every .c under $ROOT instead.
if [ "$(git -C "$ROOT" rev-parse --show-toplevel 2>/dev/null)" = "$(cd "$ROOT" && pwd -P)" ]; then
	cfiles=$(cd "$ROOT" && git ls-files '*.c')
else
	cfiles=$(tree_find "$ROOT" -type f -name '*.c')
fi
for f in $cfiles; do
	awk -v FILE="$f" -f "$detector" "$ROOT/$f" >>"$work/found" || true
done
awk -F: '{ print $1 }' "$work/found" | sort | uniq -c \
	| awk '{ print $1, $2 }' | sort -k2 >"$work/current"
sort -k2 "$baseline" >"$work/expected"

# --- compare, and report what grew and what shrank -------------------------
: >"$work/grown"
: >"$work/shrank"
while read -r count path; do
	was=$(awk -v p="$path" '$2 == p { print $1 }' "$work/expected")
	[ -n "$was" ] || was=0
	if [ "$count" -gt "$was" ]; then
		printf '%s: %s now, %s in the baseline\n' "$path" "$count" "$was" >>"$work/grown"
	elif [ "$count" -lt "$was" ]; then
		printf '%s: %s now, %s in the baseline\n' "$path" "$count" "$was" >>"$work/shrank"
	fi
done <"$work/current"
# A file the baseline names that has no site left at all
while read -r was path; do
	grep -q " $path\$" "$work/current" || printf '%s: 0 now, %s in the baseline\n' "$path" "$was" >>"$work/shrank"
done <"$work/expected"

if [ -s "$work/grown" ]; then
	printf '%s\n' "a strncat() bound that leaves no room for the terminator was added:" >&2
	cat "$work/grown" >&2
	printf '%s\n' "fix the bound (subtract one), or explain the site and update $baseline" >&2
	fail "the strncat baseline grew"
fi

if [ -s "$work/shrank" ]; then
	printf '%s\n' "fewer strncat() sites than the baseline records -- a fixed one is room for a new one:" >&2
	cat "$work/shrank" >&2
	printf '%s\n' "lower $baseline to these counts (a file with none left is removed from it)" >&2
	fail "the strncat baseline was not lowered"
fi

total=$(wc -l <"$work/found" | tr -d " ")
expected_total=$(awk '{ s += $1 } END { print s + 0 }' "$work/expected")

pass "no strncat() bound lost its room for the terminator ($total sites, baseline $expected_total)"
