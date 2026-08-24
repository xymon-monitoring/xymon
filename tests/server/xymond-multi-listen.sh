#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/xymond-multi-listen.sh
#
# xymond listens on every address it is given, and keeps a loopback listener
# unless told not to.
#
# One address is not enough for a server: the operator wants a chosen interface
# AND the loopback, so the client on the server keeps reporting when the
# interface goes away. Before --listen was fixed that came free, because every
# --listen bound the wildcard; now the set has to be expressible.
#
# Asked of a running daemon, address by address: a list binds every entry, a
# single address is unchanged, a non-loopback address still leaves 127.0.0.1
# answering, 0.0.0.0 already covers loopback, and --no-loopback leaves none.
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

# A second loopback address. Linux gives the whole 127.0.0.0/8 to lo, so
# 127.0.0.2 is bindable there; the BSDs configure only 127.0.0.1, and a bind
# elsewhere in the range fails with "Can't assign requested address". Whether
# it exists is therefore asked, not assumed -- with the daemon itself, which is
# the only prober that agrees with the daemon by construction.
LO2=127.0.0.2

# ... and one address that is NOT loopback, which is the only way to ask
# whether the daemon adds a loopback listener on its own. 127.0.0.2 cannot
# stand in for it: it is a loopback address, so a daemon told to use it has
# already satisfied the rule. A machine with no global address cannot answer
# the question at all.
# "|| true" on both: neither tool exists everywhere -- ip(8) is Linux, and a
# minimal container may have neither -- and under "set -e -o pipefail" a
# command that is not found takes the whole script down with rc=127 before it
# can print anything, which is exactly what it did on the BSDs.
EXTIP=$(ip -4 -o addr show scope global 2>/dev/null | awk '{print $4}' | cut -d/ -f1 | head -1 || true)
[ -n "$EXTIP" ] || EXTIP=$(ifconfig -a 2>/dev/null | awk '/[ \t]inet /{print $2}' | sed 's/^addr://' | grep -v '^127\.' | head -1 || true)

# answers ADDR PORT : does a xymond answer a ping there?
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
	MLPID=$!
	# Wait for it to say it is up, or to die trying.
	i=0
	while [ "$i" -lt 100 ]; do
		grep -q 'Setup complete' "$work/xymond.log" 2>/dev/null && return 0
		kill -0 "$MLPID" 2>/dev/null || return 1
		sleep 0.1
		i=$((i+1))
	done
	return 1
}
stop() { kill "$MLPID" 2>/dev/null || true; wait "$MLPID" 2>/dev/null || true; }

P1=$(free_port); P2=$(free_port)

# Is LO2 usable on this host at all?
HAVE_LO2=no
if launch --no-loopback --listen="$LO2:$P1"; then HAVE_LO2=yes; fi
stop

# --- a list binds every entry -------------------------------------------------

# Two ports on one address, so this holds wherever the suite runs: what is
# being asked is whether every entry of the list gets a socket, and a second
# port answers that as well as a second address would.
launch --listen="127.0.0.1:$P1,127.0.0.1:$P2" || { cat "$work/xymond.log" >&2; fail "xymond did not start with two listen entries"; }
answers 127.0.0.1 "$P1" || fail "the first entry in the list does not answer (127.0.0.1:$P1)"
answers 127.0.0.1 "$P2" || fail "the second entry in the list does not answer (127.0.0.1:$P2)"
stop

if [ "$HAVE_LO2" = yes ]; then
	launch --listen="127.0.0.1:$P1,$LO2:$P2" || { cat "$work/xymond.log" >&2; fail "xymond did not start with two listen addresses"; }
	answers 127.0.0.1 "$P1" || fail "the first address in the list does not answer (127.0.0.1:$P1)"
	answers "$LO2" "$P2" || fail "the second address in the list does not answer ($LO2:$P2)"
	stop
fi

# --- one address is unchanged -------------------------------------------------

launch --listen="127.0.0.1:$P1" || { cat "$work/xymond.log" >&2; fail "xymond did not start with one listen address"; }
answers 127.0.0.1 "$P1" || fail "a single address must behave as it always did"
answers 127.0.0.1 "$P2" && fail "a single entry must not answer on another port"
stop

# --- naming a non-loopback address still leaves loopback answering ------------

if [ -n "$EXTIP" ]; then
	launch --listen="$EXTIP:$P1" || { cat "$work/xymond.log" >&2; fail "xymond did not start on $EXTIP"; }
	answers "$EXTIP" "$P1" || fail "the named address must answer ($EXTIP:$P1)"
	answers 127.0.0.1 "$P1" || fail "a loopback listener must be kept even when only a non-loopback address is named"
	stop
fi

# A loopback address named outright is loopback already, so no second socket
# is added -- and none is needed.
if [ "$HAVE_LO2" = yes ]; then
	launch --listen="$LO2:$P1" || { cat "$work/xymond.log" >&2; fail "xymond did not start on $LO2"; }
	answers "$LO2" "$P1" || fail "a named loopback address must answer"
	assert_not_contains "Address already in use" "$(cat "$work/xymond.log")" \
		"a named loopback address already satisfies the rule; no second socket may be attempted"
	stop
fi

# --- 0.0.0.0 already covers loopback, and must not be double-bound ------------

launch --listen="0.0.0.0:$P1" || { cat "$work/xymond.log" >&2; fail "xymond did not start on the wildcard"; }
answers 127.0.0.1 "$P1" || fail "the wildcard must answer on loopback"
[ "$HAVE_LO2" = no ] || answers "$LO2" "$P1" || fail "the wildcard must answer on every loopback address"
assert_not_contains "Address already in use" "$(cat "$work/xymond.log")" \
	"the wildcard already covers loopback, so no second loopback socket may be attempted"
stop

# --- --no-loopback is how you say you really mean one address -----------------

if [ -n "$EXTIP" ]; then
	launch --no-loopback --listen="$EXTIP:$P1" || { cat "$work/xymond.log" >&2; fail "xymond did not start with --no-loopback"; }
	answers "$EXTIP" "$P1" || fail "the named address must still answer under --no-loopback"
	answers 127.0.0.1 "$P1" && fail "--no-loopback must leave no loopback listener"
	stop
fi

pass "xymond binds every address it is given and keeps a loopback listener"
