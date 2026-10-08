# SPDX-License-Identifier: GPL-2.0-or-later
# shellcheck shell=bash
#
# tests/lib/xymongen-board.sh -- shared scaffolding for the tests that run
# xymongen on a known status board (tests/web/xymongen-*.sh), without a xymond.
# Sourced after tests/lib/assert.sh and `require_bin XYMONGEN xymongen/xymongen`;
# not executable, so the runner never discovers it as a test.
#
# xymongen reads the board from the file BOARDDUMP names (xymongen/loaddata.c,
# load_state) instead of asking xymond, so the board is exactly what is written
# here and nothing waits on a daemon. The board below is a real xymond's answer
# to the query load_state sends, recorded verbatim, including the info and
# trends lines xymond adds on its own. Its times are left as recorded: xymongen
# draws the colour the board gives it -- turning an expired status purple is
# xymond's work, not xymongen's -- so their age changes nothing a test can see.
#
# The hosts.cfg has a page "main" with alpha.test (cpu red, conn green) and
# beta.test (conn yellow, disk green), and a subpage "sub" with gamma.test
# (conn green).
#
# board_setup
#     sets $work and writes, under it, a XYMONHOME (home/, with the shipped
#     web templates), the hosts.cfg, the board, and the directories xymongen
#     writes to. The pages go to $work/www.
# board_render LABEL [ENV=VALUE...] -- [XYMONGEN ARGS...]
#     runs xymongen with only the environment set here plus ENV=VALUE, and
#     sets RC to its exit status and ERR to its stderr ($work/LABEL.err).

board_setup() {
	work=$(mktempdir)
	mkdir -p "$work/home/etc" "$work/home/tmp" "$work/www/notes" \
		"$work/www/help" "$work/acks" "$work/logs" "$work/hist"
	cp -R "$(find_root)/xymond/webfiles" "$work/home/web"

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
}

# RC and ERR are for the caller.
# shellcheck disable=SC2034
board_render() {
	local label=$1; shift
	local envs=()
	while [ "$1" != "--" ]; do envs+=("$1"); shift; done
	shift

	# env -i: nothing from the caller's environment may decide what is rendered.
	RC=0
	(cd "$work" && env -i PATH="$PATH" \
		XYMONHOME="$work/home" HOSTSCFG="$work/home/etc/hosts.cfg" \
		XYMONTMP="$work/home/tmp" XYMONWWWDIR="$work/www" \
		XYMONACKDIR="$work/acks" XYMONSERVERLOGS="$work/logs" \
		XYMONHISTDIR="$work/hist" BOARDDUMP="$work/board" \
		${envs[@]+"${envs[@]}"} \
		"$XYMONGEN" "$@" "$work/www") > "$work/$label.out" 2> "$work/$label.err" || RC=$?
	ERR=$(cat "$work/$label.err")
}
