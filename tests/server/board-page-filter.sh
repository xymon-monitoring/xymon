#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/board-page-filter.sh
#
# The top-level page must be selectable by name from a page filter -- issue #530.
#
# The top-level page has no pagepath of its own: it is the empty string, set on
# the pagelist head in lib/loadhosts.c. An empty string is not a name that can be
# matched -- namematch() and matchregex() both refuse an empty needle -- so every
# page filter in the tree had one page it could not select, and no pattern an
# admin could write would reach the hosts on it.
#
# What that cost depended on how the filter compared. namematch() refuses an
# empty needle, so alerts.cfg's PAGE= could not select the page at all, and
# criteriamatch() has mapped "" to "/" by hand since 4abd6f3e0. The regex
# surfaces refuse only NULL, so "^$" and ".*" did reach those hosts -- what
# could not reach them was a pattern naming the page. `xymondboard page=/`
# selected nothing while `page=^$` worked, which is the opposite of what
# analysis.cfg(5) tells an admin to write.
#
# pagepath_matchname() (lib/matching.c) is now the one place that name is
# decided, and the six filter sites pass their value through it. The regex
# surfaces change meaning: "^$" stops selecting the top page and "^/$" starts.
# Both directions are asserted below, because that is a break for a filter
# written against the old behaviour.
#
# Driven through the real xymond, because `xymondboard page=...` is what an
# admin types.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
# shellcheck source=tests/lib/xymond-daemon.sh
. "$(dirname "$0")/../lib/xymond-daemon.sh"

ROOT=$(find_root)

require_bin XYMOND xymond/xymond
require_bin XYMONCLIENT common/xymon

work=$(mktempdir)

XYMOND_PID=""
stop_xymond() {
	[ -n "$XYMOND_PID" ] || return 0
	kill "$XYMOND_PID" 2>/dev/null || true
	local i=0
	while kill -0 "$XYMOND_PID" 2>/dev/null && [ "$i" -lt 100 ]; do
		sleep 0.1
		i=$((i+1))
	done
	XYMOND_PID=""
}
register_cleanup stop_xymond

# fronthost is on the top-level page -- no "page" line above it, so its pagepath
# is the empty string. subhost is on a named page.
# deephost is two levels down, so its pagepath contains a separator. Without it
# the fixture cannot see that these filters are unanchored and that "/" alone is
# therefore not a way to name the top page.
cat >"$work/hosts.cfg" <<'EOF'
127.0.0.1 fronthost # conn

page sub Subpage
127.0.0.2 subhost # conn

subpage deep Deep
127.0.0.3 deephost # conn
EOF

mkdir -p "$work/home/etc" "$work/home/tmp" "$work/home/www"
require_cfg XYMONSERVER_CFG xymond/etcfiles/xymonserver.cfg
sed -e 's|^XYMONHOME=.*|XYMONHOME="'"$work"'/home"|' \
    -e 's|^XYMONTMP=.*|XYMONTMP="'"$work"'/home/tmp"|' \
	"$XYMONSERVER_CFG" > "$work/xymonserver.cfg"

xymond_launch() {
	local port=$1; shift
	"$XYMOND" --no-daemon --listen="127.0.0.1:$port" \
		--hosts="$work/hosts.cfg" --env="$work/xymonserver.cfg" \
		--pidfile="$work/xymond.pid" \
		"$@" \
		> "$work/xymond.log" 2>&1 &
	XYMOND_PID=$!
}

start_xymond

send() { "$XYMONCLIENT" "127.0.0.1:$PORT" "$1" >/dev/null || fail "xymond rejected: $1"; }

send "status fronthost.conn green up"
send "status subhost.conn green up"
send "status deephost.conn green up"

# Wait for both to be on the board rather than for a duration.
board() { "$XYMONCLIENT" "127.0.0.1:$PORT" "xymondboard $1 fields=hostname" 2>/dev/null || true; }
i=0
while [ "$i" -lt 100 ]; do
	all=$(board '')
	case $all in *fronthost*) case $all in *deephost*) break ;; esac ;; esac
	sleep 0.1
	i=$((i+1))
done
assert_contains "fronthost" "$all" "fixture is wrong: fronthost never reached the board"
assert_contains "subhost" "$all" "fixture is wrong: subhost never reached the board"
assert_contains "deephost" "$all" "fixture is wrong: deephost never reached the board"

# The spelling to use: anchored at both ends. These filters are unanchored, so
# "/" alone also matches every pagepath containing a separator -- exactly as
# "sub" matches "subpage". That is a property of the filter, not of the name.
got=$(board 'page=^/$')
assert_contains "fronthost" "$got" \
	"xymondboard page=^/\$ did not select the top-level page, which analysis.cfg.5 names / (#530)"
assert_not_contains "subhost" "$got" \
	"xymondboard page=^/\$ selected a host on a named page (#530)"
assert_not_contains "deephost" "$got" \
	"xymondboard page=^/\$ selected a host on a nested page (#530)"

# Unanchored "/" reaches the top page too, and every nested page with it. Pinned
# so the anchoring advice above is not mistaken for pedantry.
got=$(board 'page=/')
assert_contains "fronthost" "$got" \
	"xymondboard page=/ did not reach the top-level page (#530)"
assert_contains "deephost" "$got" \
	"xymondboard page=/ stopped matching a nested pagepath, which it has always done (#530)"

# The break this change accepts. "^$" matched the top page before, because the
# subject was the empty string and matchregex() refuses only NULL. It is now "/",
# so this selects nothing. alerts.cfg.5 on main still tells admins to write it.
got=$(board 'page=^$')
assert_not_contains "fronthost" "$got" \
	"xymondboard page=^\$ still selects the top-level page: the empty pagepath is reaching the filter unnamed (#530)"

# The control: naming a real page must still work, and must not now pick up the
# top-level page along with it.
got=$(board 'page=sub')
assert_contains "subhost" "$got" \
	"xymondboard page=sub stopped selecting the page it names (#530)"
assert_not_contains "fronthost" "$got" \
	"xymondboard page=sub selected the top-level page (#530)"

pass "the top-level page is named / on every page filter, and anchoring is what selects it alone (#530)"
