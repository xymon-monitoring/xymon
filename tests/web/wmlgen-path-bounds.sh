#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/web/wmlgen-path-bounds.sh
#
# generate_wml_statuscard() builds two things from a hostname and a column
# name: the request it sends to xymond, and the path it writes the card to.
# Neither was bounded, and they fail differently.
#
#   - The request was a 1 KiB stack buffer filled with sprintf(). hosts.cfg
#     lets a hostname run to MAX_LINE_LEN (16 KiB, loadlayout.c:423), and
#     nothing between the two imposes a smaller limit, so a long name in the
#     config overflowed the stack -- before any path check and before the
#     daemon was contacted (@SoundGoof, reproduced under ASan).
#
#   - The path is bounded by PATH_MAX, which is real, so an overlong one is
#     refused. The refusal has to reach the caller: do_wml_cards() links to
#     the card from the host card, and a card it skipped silently left a link
#     to a file that does not exist. That case needs no oversized name at all
#     -- the status path is one component longer than the host card's own, so
#     a wmldir near the limit is enough to put one inside and the other out.
#
# The real function is extracted and driven, rather than reimplemented: a copy
# would keep passing after production stopped doing this.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_cc
ROOT=$(find_root)
SRC="$ROOT/xymongen/wmlgen.c"
[ -f "$SRC" ] || fail "cannot find xymongen/wmlgen.c"

work=$(mktempdir)
mkdir -p "$work/wml"

sed -n '/^static int generate_wml_statuscard/,/^}/p' "$SRC" > "$work/wmlgen-statuscard.inc"
grep -q "generate_wml_statuscard" "$work/wmlgen-statuscard.inc" \
	|| fail "could not extract generate_wml_statuscard() from wmlgen.c -- has its signature changed?"
grep -q "xymondlog" "$work/wmlgen-statuscard.inc" \
	|| fail "the extracted function does not build the daemon request; the extraction is wrong"

harness_cflags=$(xymon_cflags "$ROOT")
# shellcheck disable=SC2086  # deliberate word-splitting, as the neighbouring tests do
"$CC" $harness_cflags -I"$work" -o "$work/harness" \
	"$(dirname "$0")/wmlgen-path-bounds-harness.c" 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; fail "harness does not compile"; }

# A directory deep enough that a short name still overflows PATH_MAX, with
# every component well inside NAME_MAX. Sized in the shell rather than the
# harness because it has to exist: a single 4000-character component would
# make the harness's boundary case pass through ENAMETOOLONG at fopen(),
# whatever the length guard under test does -- measured, it did.
deep="$work/wml"
while [ ${#deep} -lt 3900 ]; do deep="$deep/$(printf 'd%.0s' $(seq 1 200))"; done
mkdir -p "$deep" 2>/dev/null || skip "cannot create a directory path near PATH_MAX here"

out=$("$work/harness" "$work/wml" "$deep" 2>&1) \
	|| fail "the status-card bounds are not held: $out"

# The overflow itself is only visible to a sanitizer: the write is past a stack
# buffer, which a plain build survives without a word.
if asan_usable; then
	# shellcheck disable=SC2086
	"$CC" $harness_cflags -fsanitize=address -g -I"$work" -o "$work/harness-asan" \
		"$(dirname "$0")/wmlgen-path-bounds-harness.c" 2>"$work/cc-asan.log" \
		|| { cat "$work/cc-asan.log" >&2; fail "sanitized harness does not compile"; }

	set +e
	"$work/harness-asan" "$work/wml" "$deep" >"$work/asan.out" 2>&1
	set -e
	# Leaks count too: the harness itself frees everything it takes, so what
	# LeakSanitizer reports here was allocated by the function under test --
	# which is how the early return for an unavailable status was dropping
	# the log buffer it had just been handed.
	if grep -q "ERROR: AddressSanitizer" "$work/asan.out"; then
		sed -n '1,12p' "$work/asan.out" >&2
		fail "a hostname longer than the request buffer overflowed it"
	fi
	if grep -q "ERROR: LeakSanitizer" "$work/asan.out"; then
		sed -n '1,14p' "$work/asan.out" >&2
		fail "the status-card path leaked memory"
	fi
else
	pass_partial "a status card refuses a path that does not fit, and its request is sized to the names" \
		"${CC:-cc} cannot build and run ASan binaries, so the overflow itself is unverified"
fi

pass "a status card refuses a path that does not fit, and its request is sized to the names"
