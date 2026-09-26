#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/xymond-listen-address.sh
#
# --listen binds the address it names, and refuses one it cannot parse.
#
# Both faults were invisible to this suite: every other daemon test passes
# --listen=127.0.0.1:PORT and then talks to 127.0.0.1, which a wildcard bind
# answers just as well. Only xymond-port-retry.sh noticed, and only on the
# BSDs, which upstream CI does not run -- so the fix had no test that would
# fail on a revert.
#
# An unparseable address must stop the daemon rather than quietly become the
# wildcard (needs nothing from the host, so it runs everywhere); a parseable
# one must be the only address bound (needs a non-loopback address here, and
# skips where there is none).
#
# Needs a built tree: xymond and the xymon client.
set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
# shellcheck source=tests/lib/xymond-daemon.sh
. "$(dirname "$0")/../lib/xymond-daemon.sh"

require_bin XYMOND xymond/xymond
require_bin XYMONCLIENT common/xymon
require_shm_segments "$(grep -c 'setup_channel(C_[A-Z_]*, CHAN_MASTER)' "$(find_root)/xymond/xymond.c")"

work=$(mktempdir)
mkdir -p "$work/home/etc" "$work/home/tmp" "$work/home/www"
printf 'page test Test\n127.0.0.1 testhost.example.com # conn\n' > "$work/hosts.cfg"

require_cfg XYMONSERVER_CFG xymond/etcfiles/xymonserver.cfg
sed -e 's|^XYMONHOME=.*|XYMONHOME="'"$work"'/home"|' \
    -e 's|^XYMONTMP=.*|XYMONTMP="'"$work"'/home/tmp"|' \
	"$XYMONSERVER_CFG" > "$work/xymonserver.cfg"

# A non-loopback address of this host. It is the only way to ask whether the
# daemon bound more than it was told: a second loopback address cannot answer
# that, and 0.0.0.0 is what the bug produced. A host with none skips that half.
#
# "|| true" on both: ip(8) is Linux-only and a minimal container may have
# neither tool, and under "set -e -o pipefail" a command that is not found
# takes the script down with rc=127 before it can print anything.
EXTIP=$(ip -4 -o addr show scope global 2>/dev/null | awk '{print $4}' | cut -d/ -f1 | head -1 || true)
[ -n "$EXTIP" ] || EXTIP=$(ifconfig -a 2>/dev/null | awk '/[ \t]inet /{print $2}' | sed 's/^addr://' | grep -v '^127\.' | head -1 || true)

# answers ADDR PORT : does a xymond answer a ping there?
answers() {
	case "$(XYMON_TIMEOUT=2 "$XYMONCLIENT" "$1:$2" "ping" 2>&1)" in
		*xymond*) return 0 ;;
	esac
	return 1
}

# launch ARGS... : start a daemon and wait for it to say it is up, or to die
# trying. Returns non-zero if it never came up.
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

P=$(free_port)

# --- an unparseable address stops the daemon ----------------------------------

# 999.999.999.999 rather than a hostname-shaped string: it is invalid to any
# parser, so this stays a test of "refuse what you cannot parse" even if
# --listen ever learns to resolve names.
if launch --listen="999.999.999.999:$P"; then
	stop
	fail "xymond started with an unparseable --listen address -- it is binding the wildcard instead of refusing, the way it did before the address was checked"
fi
stop
answers 127.0.0.1 "$P" && fail "something is answering on 127.0.0.1:$P after a rejected --listen"
assert_contains "Cannot parse listen address" "$(cat "$work/xymond.log")" \
	"a rejected address must say so"

# --- a parseable address is the only one bound --------------------------------

launch --listen="127.0.0.1:$P" || { cat "$work/xymond.log" >&2; fail "xymond did not start on 127.0.0.1:$P"; }
answers 127.0.0.1 "$P" || fail "xymond does not answer on the address it was given (127.0.0.1:$P)"
if [ -n "$EXTIP" ]; then
	answers "$EXTIP" "$P" && fail "xymond answers on $EXTIP:$P but was told to listen on 127.0.0.1 only -- it is binding every interface"
fi
stop

pass "xymond binds the address --listen names, and refuses one it cannot parse"
