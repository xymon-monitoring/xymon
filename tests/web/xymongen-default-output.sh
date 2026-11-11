#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/web/xymongen-default-output.sh [--update]
#
# With the default configuration, the pages xymongen writes stay byte for byte
# what they were: a change that alters them without meaning to fails here.
# The pages are rendered from the shared status board
# (tests/lib/xymongen-board.sh) and compared with the reference pages in
# tests/fixtures/xymongen-default-output/, after masking what changes from run
# to run: the date and time ("Wed Nov 11 12:25:08 2026"), the age of each
# status in the icon's ALT and TITLE ("33d12h17m"), and the version in the
# footer.
#
# A change that alters the pages on purpose regenerates the reference:
#
#     tests/web/xymongen-default-output.sh --update
#
# and commits it with the change, so that the difference shows in its diff,
# where a reviewer judges it. --update refuses to save the pages of a run that
# failed: xymongen exiting non-zero, or one of the checks below failing.
#
# Besides the comparison, the test checks two things on its own, so that a
# broken reference cannot become the norm unnoticed: every status of the board
# is shown in its colour, and every page that refreshes keeps its META
# refresh.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
# shellcheck source=tests/lib/xymongen-board.sh
. "$(dirname "$0")/../lib/xymongen-board.sh"

update=0
case "${1:-}" in
"") ;;
--update) update=1 ;;
*) fail "usage: $0 [--update]" ;;
esac

require_bin XYMONGEN xymongen/xymongen

ROOT=$(find_root)
reference=$ROOT/tests/fixtures/xymongen-default-output

board_setup
board_render default --
if [ "$RC" -ne 0 ]; then
	printf '%s\n' "$ERR" >&2
	fail "xymongen exited with status $RC; nothing compared or saved"
fi

# The pages, masked, as a tree under $work/masked.
masked=$work/masked
(cd "$work/www" && find . -type f) | LC_ALL=C sort > "$work/pages"
[ -s "$work/pages" ] || fail "xymongen wrote no pages"
while IFS= read -r p; do
	mkdir -p "$masked/$(dirname "$p")"
	sed -E \
		-e 's/[A-Z][a-z]{2} [A-Z][a-z]{2} [ 0-9][0-9] [0-9]{2}:[0-9]{2}:[0-9]{2} [0-9]{4}/@DATE@/g' \
		-e 's/:([0-9]+[dhm])+"/:@AGE@"/g' \
		-e 's/>Xymon [0-9][^<]*</>Xymon @VERSION@</g' \
		"$work/www/$p" > "$masked/$p"
done < "$work/pages"

# Every status of the board is shown in its colour: on its host's page, the
# cell for that test links to it and shows the colour's icon. The info and
# trends columns are links, not statuses, and keep the icon xymonserver.cfg
# gives them.
while IFS='|' read -r host test color _; do
	case "$test" in info|trends) continue ;; esac
	re="HOST=${host//./\\.}&amp;SERVICE=$test\"><IMG SRC=\"[^\"]*/$color\\.gif\""
	found=0
	while IFS= read -r p; do
		[[ $(cat "$work/www/$p") =~ $re ]] && { found=1; break; }
	done < "$work/pages"
	[ "$found" = 1 ] || fail "no page shows $host's $test as $color, as the board has it"
done < "$work/board"

# Every page whose header template refreshes keeps the META refresh, with the
# default interval, as a line of its own: inside a <noscript>, or any other
# wrapper, it would no longer refresh a page that runs scripts.
for p in xymon.html nongreen.html critical.html red.html main/main.html main/sub/sub.html; do
	[ -f "$work/www/$p" ] || fail "xymongen did not write $p"
	grep -Fqx '<META HTTP-EQUIV="REFRESH" CONTENT="60">' "$work/www/$p" \
		|| fail "$p has lost its META refresh line, or wraps it in something"
done

if [ "$update" = 1 ]; then
	rm -rf "$reference"
	mkdir -p "$reference"
	cp -R "$masked/." "$reference/"
	pass "saved $(wc -l < "$work/pages" | tr -d ' ') pages as the reference in tests/fixtures/xymongen-default-output; commit them with the change"
fi

[ -d "$reference" ] || fail "no reference pages in $reference; run $0 --update"

# The same set of pages.
(cd "$reference" && find . -type f) | LC_ALL=C sort > "$work/refpages"
if ! cmp -s "$work/refpages" "$work/pages"; then
	diff "$work/refpages" "$work/pages" >&2 || true
	fail "xymongen writes a different set of pages than the reference (< reference, > now)"
fi

# The same references to other files first (rule 3 of the facelift spec:
# nothing extra loaded by default), then the same bytes.
while IFS= read -r p; do
	if ! cmp -s "$reference/$p" "$masked/$p"; then
		grep -Eo '(HREF|SRC|href|src)="[^"]*"' "$reference/$p" | LC_ALL=C sort -u > "$work/ref.links" || true
		grep -Eo '(HREF|SRC|href|src)="[^"]*"' "$masked/$p" | LC_ALL=C sort -u > "$work/now.links" || true
		if ! cmp -s "$work/ref.links" "$work/now.links"; then
			diff "$work/ref.links" "$work/now.links" >&2 || true
			fail "$p references other files than the reference (< reference, > now)"
		fi
		diff -u "$reference/$p" "$masked/$p" >&2 || true
		fail "$p differs from the reference; if that is meant, run $0 --update and commit the result"
	fi
done < "$work/pages"

pass "the $(wc -l < "$work/pages" | tr -d ' ') pages xymongen writes with the default configuration match the reference, every status in its colour and every refreshing page with its META refresh"
