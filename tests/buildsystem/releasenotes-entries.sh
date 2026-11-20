#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/buildsystem/releasenotes-entries.sh
#
# A pull request writes its RELEASENOTES entry as a file of its own in
# releasenotes.d/, so that two of them never write to the same place, and
# build/releasenotes.sh moves the entries into RELEASENOTES at release.
#
# First the tree's own entries: each is a title "component -- what changes",
# dashes of its length and prose within the bounds RELEASING.md sets, or the
# release would stop on it. Then the script, on a copy: the
# entries go under the open section, above what it holds, in the order of
# their names; the heading closes; their files go and README stays. With no
# open section one is opened above the last release and the title box is
# set. Whatever it refuses, it refuses before writing anything.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
[ -f "$ROOT/build/releasenotes.sh" ] || skip "no build/releasenotes.sh in this checkout"

out=$(cd "$ROOT" && sh build/releasenotes.sh --check 2>&1) || fail "an entry of releasenotes.d is not written as its README says: $out"

work=$(mktempdir)
run() { (cd "$work" && sh "$ROOT/build/releasenotes.sh" "$@" 2>&1); }

# seed OPEN -- a tree with RELEASENOTES as the real one begins, its top
# section open (OPEN=open) or the last release's (OPEN=closed), and no entry.
seed() {
	rm -rf "$work/releasenotes.d" "$work/RELEASENOTES"
	mkdir "$work/releasenotes.d"
	echo "how to write an entry" >"$work/releasenotes.d/README"
	{
		echo '          <<<<<<<<<<<<<<<<<<<<<<<<>>>>>>>>>>>>>>>>>>>>>>>>'
		echo '           * * *   Release notes for Xymon 4.3.31   * * *'
		echo '          <<<<<<<<<<<<<<<<<<<<<<<<>>>>>>>>>>>>>>>>>>>>>>>>'
		echo
		echo 'This documents the important changes between Xymon releases.'
		echo
		if [ "$1" = open ]; then
			printf '%s\n' 'Changes for 4.3.32 (not yet released)' '=====================================' '' \
				'older -- written in the file itself' '-----------------------------------' 'It was here before.' ''
		fi
		printf '%s\n' 'Changes for 4.3.31' '==================' '' 'last -- of the last release' '---------------------------' 'Shipped.'
	} >"$work/RELEASENOTES"
}
# entry NAME TITLE PROSE -- an entry file, well written.
entry() { printf '%s\n%s\n%s\n' "$2" "$(printf '%s' "$2" | sed 's/./-/g')" "$3" >"$work/releasenotes.d/$1.txt"; }
# lines N -- N lines of prose, the last without its newline.
lines() { local i out=""; for ((i = 1; i <= $1; i++)); do out="$out${out:+$'\n'}line $i"; done; printf '%s' "$out"; }
notes() { cat "$work/RELEASENOTES"; }
lineno() { awk -v want="$1" '$0 == want && !n { n = NR } END { print n + 0 }' "$work/RELEASENOTES"; }

# An open section: the entries go under its heading, above what it held.
seed open
entry xymonnet-b 'xymonnet -- second by name' 'Prose of b.'
entry client-a 'client -- first by name' 'Prose of a.'
out=$(run 4.3.32) || fail "releasenotes.sh failed on an open section: $out"
assert_contains '2 entries moved in' "$out" "what it says"
assert_not_contains 'not yet released' "$(notes)" "the heading is closed"
[ "$(lineno 'Changes for 4.3.32')" -lt "$(lineno 'client -- first by name')" ] || fail "the entries are not under the heading: $(notes)"
[ "$(lineno 'client -- first by name')" -lt "$(lineno 'xymonnet -- second by name')" ] || fail "the entries are not in the order of their names: $(notes)"
[ "$(lineno 'xymonnet -- second by name')" -lt "$(lineno 'older -- written in the file itself')" ] || fail "the entries are not above what the section held: $(notes)"
assert_equal '==================' "$(sed -n "$(($(lineno 'Changes for 4.3.32') + 1))p" "$work/RELEASENOTES")" "the underline is as long as the closed heading"
assert_equal '' "$(sed -n "$(($(lineno 'Changes for 4.3.32') + 2))p" "$work/RELEASENOTES")" "an empty line under it"
assert_equal 'client -- first by name' "$(sed -n "$(($(lineno 'Changes for 4.3.32') + 3))p" "$work/RELEASENOTES")" "then the first entry"
assert_equal '' "$(sed -n "$(($(lineno 'older -- written in the file itself') - 1))p" "$work/RELEASENOTES")" "one empty line between the last entry and what was there"
assert_equal 'Prose of b.' "$(sed -n "$(($(lineno 'older -- written in the file itself') - 2))p" "$work/RELEASENOTES")" "and only one"
assert_contains 'Shipped.' "$(notes)" "the released sections are kept"
assert_equal 'README' "$(ls "$work/releasenotes.d")" "the entries' files are removed, README kept"
assert_equal '' "$(ls "$work" | grep -v -x -e RELEASENOTES -e releasenotes.d || true)" "nothing is left beside them"

# An open section and no entry: the heading closes all the same.
seed open
out=$(run 4.3.32) || fail "releasenotes.sh failed with no entry: $out"
assert_not_contains 'not yet released' "$(notes)" "closed with no entry"
assert_contains 'older -- written in the file itself' "$(notes)" "what the section held is kept"

# No section yet: one is opened above the last release, the box is set.
seed closed
entry web-a 'web -- the first entry of a release' 'Prose.'
out=$(run 4.3.32) || fail "releasenotes.sh failed with no open section: $out"
assert_contains '* * *   Release notes for Xymon 4.3.32   * * *' "$(notes)" "the title box names the release"
assert_not_contains 'Release notes for Xymon 4.3.31' "$(notes)" "and no longer the last one"
[ "$(lineno 'Changes for 4.3.32')" -lt "$(lineno 'web -- the first entry of a release')" ] || fail "no section opened above the entry: $(notes)"
[ "$(lineno 'web -- the first entry of a release')" -lt "$(lineno 'Changes for 4.3.31')" ] || fail "the new section is not above the last release's: $(notes)"
assert_equal '' "$(sed -n "$(($(lineno 'Changes for 4.3.31') - 1))p" "$work/RELEASENOTES")" "an empty line before the last release's section"

# Neither: nothing to say, nothing written.
seed closed
before=$(notes)
out=$(run 4.3.32) || fail "releasenotes.sh failed with nothing to do: $out"
assert_equal "$before" "$(notes)" "no entry and no open section: left alone"

# Run again after the release: closed already.
out=$(run 4.3.31) || fail "releasenotes.sh failed on a section closed already: $out"
assert_equal "$before" "$(notes)" "a section closed already is left alone"

# refused WHAT ARGS... -- the run fails, and RELEASENOTES and the entries are
# as they were.
refused() {
	local what=$1 before entries; shift
	before=$(notes); entries=$(ls "$work/releasenotes.d")
	if out=$(run "$@"); then fail "releasenotes.sh accepted $what: $out"; fi
	assert_equal "$before" "$(notes)" "$what: RELEASENOTES untouched"
	assert_equal "$entries" "$(ls "$work/releasenotes.d")" "$what: the entries untouched"
}

seed open; entry good 'good -- a fine entry' 'Prose.'
printf 'web -- t\n---\nprose\n' >"$work/releasenotes.d/short.txt"
refused "dashes shorter than the title" 4.3.32
assert_contains 'short.txt' "$out" "it names the file"

seed open; printf 'web -- t\n--------\n' >"$work/releasenotes.d/empty.txt"
refused "an entry with no prose" 4.3.32

seed open; printf 'web -- t\n--------\nprose' >"$work/releasenotes.d/nonl.txt"
refused "a last line with no newline" 4.3.32

seed open; printf 'web -- t\n--------\nprose\n\nChanges for 9.9.9\n=================\n' >"$work/releasenotes.d/section.txt"
refused "an entry that opens a section" 4.3.32

seed open; printf 'web -- t\n--------\n<<<<<<< HEAD\nprose\n' >"$work/releasenotes.d/marker.txt"
refused "a conflict marker" 4.3.32

# What RELEASING.md asks of an entry, as far as it can be measured.
seed open; entry bad 'A title with no component' 'Prose.'
refused "a title that is not component -- what changes" 4.3.32
assert_contains 'is not a title' "$out" "it says why"

seed open; entry bad "web -- $(printf '%075d' 0)" 'Prose.'
refused "a title over 80 columns" 4.3.32
assert_contains 'columns wide' "$out" "it says why"

seed open; entry bad 'web -- a title' "$(printf '%081d' 0)"
refused "a line of prose over 80 columns" 4.3.32
assert_contains 'line 3 is 81 columns wide' "$out" "it says which line"

seed open; entry bad 'web -- a title' "$(printf 'one\n\ntwo\n\nthree')"
refused "three paragraphs" 4.3.32
assert_contains '3 paragraphs' "$out" "it counts them"

seed open; entry bad 'web -- a title' 'Fixed in #123.'
refused "a pull request named by its number" 4.3.32
assert_contains 'by its number' "$out" "it says why"

# The bounds themselves pass: 80 columns, two paragraphs, of any length.
seed open
entry edge "web -- $(printf '%073d' 0)" "$(printf '%080d' 0)
$(lines 4)

$(lines 20)"
out=$(run 4.3.32) || fail "releasenotes.sh refused an entry at the bounds: $out"

seed open; entry good 'good -- a fine entry' 'Prose.'; echo x >"$work/releasenotes.d/notes.md"
refused "a file that is not NAME.txt" 4.3.32

seed open; entry good 'good -- a fine entry' 'Prose.'
refused "another version than the open section's" 4.4.0
assert_contains 'rename it first' "$out" "it says what to do"

seed closed; entry late 'late -- after the release' 'Prose.'
refused "an entry for a section closed already" 4.3.31

seed open
for bad in "" 4.3 4.3.32.1 4.3.x rel-4.3.32 4.3.32-rc1; do
	refused "'$bad' as a version" "$bad"
done

# --check reads and changes nothing.
seed open; entry good 'good -- a fine entry' 'Prose.'
before=$(notes)
run --check >/dev/null || fail "--check refused a well written entry"
assert_equal "$before" "$(notes)" "--check leaves RELEASENOTES alone"
assert_equal 'README good.txt' "$(ls "$work/releasenotes.d" | LC_ALL=C sort | tr '\n' ' ' | sed 's/ $//')" "and the entries"
printf 'web -- t\n---\nprose\n' >"$work/releasenotes.d/short.txt"
if run --check >/dev/null; then fail "--check accepted dashes shorter than the title"; fi

pass "releasenotes.d's entries are well written, and build/releasenotes.sh moves them into RELEASENOTES at release, or refuses before writing"
