#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/xymond-loopback-fallback.sh
#
# A listen address that cannot be bound is fatal when the operator named it,
# and is not when xymond added it itself.
#
# The loopback listener is an extra the daemon adds, so refusing to start over
# it would be the worse failure; an address that was asked for is the opposite.
# One `return` inverts either direction, so both are pinned. The named half
# runs everywhere; the loopback half needs a non-loopback address to name
# instead, and skips where there is none.
#
# The port is held by port-blocker.c, which binds without listening. That
# reserves it on the BSDs too: SO_REUSEADDR lets a wildcard and a specific bind
# overlap, but not two binds of the same address and port.
#
# Needs a built tree: xymond, the xymon client, and a C compiler.
set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
# shellcheck source=tests/lib/xymond-daemon.sh
. "$(dirname "$0")/../lib/xymond-daemon.sh"

require_bin XYMOND xymond/xymond
require_bin XYMONCLIENT common/xymon
require_shm_segments "$(grep -c 'setup_channel(C_[A-Z_]*, CHAN_MASTER)' "$(find_root)/xymond/xymond.c")"
require_c_buildenv "$(find_root)"

work=$(mktempdir)
mkdir -p "$work/home/etc" "$work/home/tmp" "$work/home/www"
printf 'page test Test\n127.0.0.1 testhost.example.com # conn\n' > "$work/hosts.cfg"

require_cfg XYMONSERVER_CFG xymond/etcfiles/xymonserver.cfg
sed -e 's|^XYMONHOME=.*|XYMONHOME="'"$work"'/home"|' \
    -e 's|^XYMONTMP=.*|XYMONTMP="'"$work"'/home/tmp"|' \
	"$XYMONSERVER_CFG" > "$work/xymonserver.cfg"

# "|| true" on both: ip(8) is Linux-only and a minimal container may have
# neither tool, and under "set -e -o pipefail" a command that is not found
# takes the script down with rc=127 before it can print anything.
EXTIP=$(ip -4 -o addr show scope global 2>/dev/null | awk '{print $4}' | cut -d/ -f1 | head -1 || true)
[ -n "$EXTIP" ] || EXTIP=$(ifconfig -a 2>/dev/null | awk '/[ \t]inet /{print $2}' | sed 's/^addr://' | grep -v '^127\.' | head -1 || true)

answers() {
	case "$(XYMON_TIMEOUT=2 "$XYMONCLIENT" "$1:$2" "ping" 2>&1)" in
		*xymond*) return 0 ;;
	esac
	return 1
}

launch() {
	"$XYMOND" --no-daemon "$@" \
		--hosts="$work/hosts.cfg" --env="$work/xymonserver.cfg" \
		--pidfile="$work/xymond.pid" --checkpoint-file="$work/chk.out" \
		> "$work/xymond.log" 2>&1 &
	LPID=$!
	local i=0
	while [ "$i" -lt 100 ]; do
		grep -q 'Setup complete' "$work/xymond.log" 2>/dev/null && return 0
		kill -0 "$LPID" 2>/dev/null || return 1
		sleep 0.1
		i=$((i+1))
	done
	return 1
}
stop() { kill "$LPID" 2>/dev/null || true; wait "$LPID" 2>/dev/null || true; }

# --- hold a loopback port ------------------------------------------------------

"$CC" -o "$work/port-blocker" "$(find_root)/tests/lib/port-blocker.c" 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; skip "port-blocker does not compile"; }
"$work/port-blocker" > "$work/blocked.port" &
BLOCKER_PID=$!
register_cleanup "kill $BLOCKER_PID 2>/dev/null || true"

i=0
while [ "$i" -lt 50 ]; do
	[ -s "$work/blocked.port" ] && break
	kill -0 "$BLOCKER_PID" 2>/dev/null || fail "port-blocker exited before naming its port"
	sleep 0.1
	i=$((i+1))
done
[ -s "$work/blocked.port" ] || fail "port-blocker never named its port"
BP=$(cat "$work/blocked.port")

# The premise, asked the way xymond asks: nothing else may bind that port.
if "$work/port-blocker" "$BP"; then
	fail "127.0.0.1:$BP is still bindable while port-blocker holds it, so neither collision below would happen"
fi

# --- a named address that cannot bind is fatal --------------------------------

if launch --listen="127.0.0.1:$BP"; then
	stop
	fail "xymond started although the address it was told to listen on (127.0.0.1:$BP) could not be bound -- an address the operator named must not be silently dropped"
fi
stop
assert_contains "Cannot bind to listen socket" "$(cat "$work/xymond.log")" \
	"the daemon must fail on the bind, not for some other reason"
# It must stop AT the named address, not carry on to the loopback extra. Made
# fatal-by-accident this reads the same: with the return dropped, the daemon
# falls through, tries the loopback on the compiled default port, and exits
# only because that port happened to be busy too -- so "it did not start" alone
# does not distinguish the two. Where the default port is free it would start,
# on an address nobody asked for, and the check above catches that instead.
assert_not_contains "Continuing without a loopback listener" "$(cat "$work/xymond.log")" \
	"a named address that cannot bind must stop the daemon there, not fall through to the loopback it adds itself"

# --- the loopback extra that cannot bind is not -------------------------------

if [ -n "$EXTIP" ]; then
	# Same blocked port, but named on a different address: the operator's
	# address binds, and the loopback xymond adds for itself collides.
	launch --listen="$EXTIP:$BP" \
		|| { cat "$work/xymond.log" >&2; fail "xymond refused to start because the loopback listener it adds itself could not bind -- that extra is best-effort, and a busy 127.0.0.1:$BP must not stop a server whose own address is free"; }
	answers "$EXTIP" "$BP" || fail "the named address must answer ($EXTIP:$BP)"
	assert_contains "Continuing without a loopback listener" "$(cat "$work/xymond.log")" \
		"dropping the loopback listener must be logged, not silent"
	stop
fi

pass "a named listen address that cannot bind is fatal; the loopback xymond adds itself is not"
