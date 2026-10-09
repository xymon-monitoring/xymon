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
# No xymond is started. xymongen reads the board from the file BOARDDUMP names
# (xymongen/loaddata.c, load_state) instead of asking xymond, so the board is
# exactly what the test writes and nothing waits on a daemon. The board below
# is a real xymond's answer to the query load_state sends, recorded verbatim,
# including the info and trends lines xymond adds on its own. Its times are
# left as recorded: xymongen draws the colour the board gives it -- turning an
# expired status purple is xymond's work, not xymongen's -- so their age
# changes nothing asserted here.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin XYMONGEN xymongen/xymongen
ROOT=$(find_root)

work=$(mktempdir)
mkdir -p "$work/home/etc" "$work/home/tmp" "$work/www/notes" \
	"$work/www/help" "$work/acks" "$work/logs" "$work/hist"
cp -R "$ROOT/xymond/webfiles" "$work/home/web"

cat > "$work/home/etc/hosts.cfg" <<'HOSTS'
page main Main
127.0.0.1 alpha.test # conn
127.0.0.2 beta.test # conn
subpage sub Sub
127.0.0.3 gamma.test # conn
HOSTS

# hostname|testname|color|flags|lastchange|logtime|validtime|acktime|disabletime|sender|cookie|line1|acklist
cat > "$work/board" <<'BOARD'
alpha.test|trends|green||0|0|0|0|0||||
alpha.test|info|green||0|0|0|0|0||||
alpha.test|cpu|red||1791500883|1791500883|1791502683|0|0|127.0.0.1|819012|red alpha cpu is high|
alpha.test|conn|green||1791500883|1791500883|1791502683|0|0|127.0.0.1||green alpha conn ok|
beta.test|trends|green||0|0|0|0|0||||
beta.test|info|green||0|0|0|0|0||||
beta.test|disk|green||1791500883|1791500883|1791502683|0|0|127.0.0.1||green beta disk ok|
beta.test|conn|yellow||1791500883|1791500883|1791502683|0|0|127.0.0.1|785440|yellow beta conn slow|
gamma.test|trends|green||0|0|0|0|0||||
gamma.test|info|green||0|0|0|0|0||||
gamma.test|conn|green||1791500883|1791500883|1791502683|0|0|127.0.0.1||green gamma conn ok|
BOARD

# env -i: nothing from the caller's environment may decide what is rendered.
rc=0
(cd "$work" && env -i PATH="$PATH" \
	XYMONHOME="$work/home" HOSTSCFG="$work/home/etc/hosts.cfg" \
	XYMONTMP="$work/home/tmp" XYMONWWWDIR="$work/www" \
	XYMONACKDIR="$work/acks" XYMONSERVERLOGS="$work/logs" \
	XYMONHISTDIR="$work/hist" BOARDDUMP="$work/board" \
	"$XYMONGEN" "$work/www" > "$work/xymongen.out" 2> "$work/xymongen.err") || rc=$?
if [ "$rc" -ne 0 ]; then
	cat "$work/xymongen.err" >&2
	fail "xymongen exited with status $rc"
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
