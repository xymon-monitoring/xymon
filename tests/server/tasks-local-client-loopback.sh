#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/tasks-local-client-loopback.sh
#
# The Xymon client running on a server sends to xymond over loopback, so the
# server keeps reporting on itself when a network interface goes down. That
# is set by the [xymonclient] task in tasks.cfg, which only a server installs:
# its own xymonclient.cfg is the same file remote clients get, and there
# XYMSRV must stay the server's address.
#
# This runs the shipped [xymonclient] section under the real xymonlaunch, with
# a client config naming a remote server address and a stub in place of
# xymonclient.sh that records the XYMSRV it was started with.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
require_bin XYMONLAUNCH "common/xymonlaunch"
# The template, not the built tasks.cfg: the build has already replaced
# @XYMONTOPDIR@ with the real install path, which this sandbox replaces too.
TASKS_CFG="$ROOT/xymond/etcfiles/tasks.cfg.DIST"
[ -f "$TASKS_CFG" ] || fail "tasks.cfg.DIST is missing"

work=$(mktempdir)
mkdir -p "$work/top/client/etc" "$work/top/client/bin" "$work/logs"

# What a remote client's config says: the server's address.
printf 'XYMSRV="10.9.9.9"\n' > "$work/top/client/etc/xymonclient.cfg"

cat > "$work/top/client/bin/xymonclient.sh" <<EOF
#!/bin/sh
printf '%s\n' "\$XYMSRV" > "$work/seen"
EOF
chmod +x "$work/top/client/bin/xymonclient.sh"

# The shipped section, pointed at this sandbox. NEEDS is dropped: there is no
# xymond task here for it to wait on. INTERVAL is dropped too: xymonlaunch
# counts it on the monotonic clock from 0, so on a host up for less than the
# interval -- a fresh CI machine -- the first run would wait that long (#381).
awk '/^\[xymonclient\]/{on=1; print; next} on && /^\[/{exit} on && /^[[:space:]]/{print}' "$TASKS_CFG" \
	| sed -e "s|@XYMONTOPDIR@|$work/top|g" -e '/NEEDS/d' -e '/INTERVAL/d' > "$work/tasks.cfg"
grep -q 'xymonclient.sh' "$work/tasks.cfg" \
	|| fail "no [xymonclient] task found in the shipped tasks.cfg"

# --debug: if the task never starts, the log says how far xymonlaunch got.
XYMONSERVERLOGS="$work/logs" "$XYMONLAUNCH" --config="$work/tasks.cfg" --no-daemon --debug \
	> "$work/launch.log" 2>&1 &
launcher=$!
register_cleanup "kill $launcher 2>/dev/null || true"

i=0
while [ ! -s "$work/seen" ] && [ "$i" -lt 100 ]; do sleep 0.2; i=$((i + 1)); done
[ -s "$work/seen" ] || { cat "$work/tasks.cfg" "$work/launch.log" "$work/logs/xymonclient.log" >&2 2>/dev/null; fail "xymonlaunch never started the [xymonclient] task"; }

assert_equal "127.0.0.1" "$(cat "$work/seen")" \
	"the client on the server was started with XYMSRV from xymonclient.cfg, not loopback: it stops reporting when the network interface goes down"

pass "the client on the server sends to xymond over loopback, whatever its xymonclient.cfg names"
