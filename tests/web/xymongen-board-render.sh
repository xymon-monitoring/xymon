#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/web/xymongen-board-render.sh
#
# The overview a user opens: from a small hosts.cfg and a known status board,
# xymongen writes every host on its page, each status with its colour, each
# page link with the worst colour below it, and a non-green page holding only
# the hosts that are not green.
#
# The board, the hosts.cfg and the run of xymongen are in
# tests/lib/xymongen-board.sh, shared with the other xymongen tests: no xymond
# is started, xymongen reads a recorded board from a file.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
# shellcheck source=tests/lib/xymongen-board.sh
. "$(dirname "$0")/../lib/xymongen-board.sh"

require_bin XYMONGEN xymongen/xymongen

board_setup
board_render render --
if [ "$RC" -ne 0 ]; then
	printf '%s\n' "$ERR" >&2
	fail "xymongen exited with status $RC"
fi

page() {
	[ -f "$work/www/$1" ] || fail "xymongen did not write $1"
	cat "$work/www/$1"
}
main=$(page main/main.html)
sub=$(page main/sub/sub.html)
top=$(page xymon.html)
nongreen=$(page nongreen.html)

# cell PAGE-TEXT PAGE-NAME HOST TEST COLOR -- the cell for HOST's TEST links to
# that status and shows COLOR. One pattern ties the three together, so a colour
# on the wrong host or the wrong test does not satisfy it.
cell() {
	local re="HOST=${3//./\\.}&amp;SERVICE=$4\"><IMG SRC=\"[^\"]*/$5\\.gif\" ALT=\"$4:$5:"
	[[ $1 =~ $re ]] || fail "$2: no $5 $4 cell for $3"
}
# pagelink PAGE-TEXT PAGE-NAME TARGET COLOR -- the link to TARGET shows COLOR.
pagelink() {
	local re="HREF=\"[^\"]*/${3//./\\.}\"><IMG SRC=\"[^\"]*/$4\\.gif\""
	[[ $1 =~ $re ]] || fail "$2: the link to $3 is not shown $4"
}

cell "$main" main.html alpha.test conn green
cell "$main" main.html alpha.test cpu red
cell "$main" main.html beta.test conn yellow
cell "$main" main.html beta.test disk green
cell "$sub" sub.html gamma.test conn green

# Each host on its own page only.
assert_not_contains 'NAME="gamma.test"' "$main" "main.html lists gamma.test, which hosts.cfg puts on the subpage"
assert_not_contains 'NAME="alpha.test"' "$sub" "sub.html lists alpha.test, which hosts.cfg puts on the main page"

# A page link carries the worst colour under it: red for main (alpha's cpu),
# green for the subpage, whose only host is green.
pagelink "$top" xymon.html main/main.html red
pagelink "$main" main.html main/sub/sub.html green

# The non-green page holds the hosts that are not green, and only those.
cell "$nongreen" nongreen.html alpha.test cpu red
cell "$nongreen" nongreen.html beta.test conn yellow
assert_not_contains 'NAME="gamma.test"' "$nongreen" "nongreen.html lists gamma.test, whose only status is green"

pass "xymongen renders each host on its page with its colours, and page links with the worst colour below them"
