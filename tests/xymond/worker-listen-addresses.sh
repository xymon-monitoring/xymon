#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/xymond/worker-listen-addresses.sh
#
# A network worker's --listen and --locatorid take an address as the locator
# does: an IPv4 short form such as 127.1 still works, as inet_aton() took it,
# and a malformed one is refused with a message instead of crashing.
#
# Checked with xymond_rrd in network mode against a locator on 127.0.0.1:
# --listen=127.1:PORT takes connections on 127.0.0.1:PORT, and none on
# 127.0.0.2:PORT, so it bound 127.0.0.1 and not every address. That second
# check counts only where a worker listening on every address (--listen=:PORT)
# is reached at 127.0.0.2, as on Linux, where all of 127/8 is loopback;
# elsewhere it is reported as not checked. --listen=localhost:PORT takes
# connections on 127.0.0.1:PORT, where /etc/hosts says so. A malformed
# --listen (with --locatorid valid, so the listener is what reads it) and a
# malformed --locatorid each stop it with a message, and no signal.
# Connections are made by the xymon client, whose XYMON_TIMEOUT bounds them.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin XYMOND_LOCATOR xymond/xymond_locator
# ps finds the children the worker's listener forked; a minimal container may
# not have it.
command -v ps >/dev/null 2>&1 \
	|| skip "ps not available (needed to stop the worker's connection children)"
require_bin XYMOND_RRD xymond/xymond_rrd
require_bin XYMONCMD common/xymoncmd
require_bin XYMONCLIENT common/xymon
require_bin LOCATOR lib/xymonlocator
# shellcheck source=tests/lib/xymond-daemon.sh
. "$(dirname "$0")/../lib/xymond-daemon.sh"

work=$(mktempdir)
# Four distinct ports from free_port, which draws below the kernel's
# ephemeral range: a port inside it can be held by an outgoing connection,
# and a worker that cannot bind fails the test.
taken=""
for v in lport p1 p2 p3; do
	while :; do
		p=$(free_port) || fail "no free port for the locator and the workers"
		case " $taken " in *" $p "*) continue ;; esac
		break
	done
	taken="$taken $p"
	printf -v "$v" '%s' "$p"
done
mkdir -p "$work"/{home/etc,home/tmp,logs,rrd}
printf '127.0.0.11 hosta # noconn\n' >"$work/home/etc/hosts.cfg"
{
	printf 'XYMONHOME="%s/home"\nXYMONTMP="%s/home/tmp"\n' "$work" "$work"
	printf 'HOSTSCFG="%s/home/etc/hosts.cfg"\nXYMONDPORT="%s"\n' "$work" "$lport"
} >"$work/xymonserver.cfg"
export XYMONTMP="$work"

"$XYMOND_LOCATOR" --listen="127.0.0.1:$lport" --logfile="$work/logs/locator.log" 2>"$work/logs/locator.err" &
register_cleanup "stop_process $!"
up=
for _ in {1..50}; do
	grep -q "Locator is available" <<<"$("$LOCATOR" "127.0.0.1:$lport" </dev/null 2>/dev/null)" && { up=1; break; }
	sleep 0.1
done
[ -n "$up" ] || fail "the locator did not come up on 127.0.0.1:$lport: $(cat "$work/logs/locator.err" "$work/logs/locator.log" 2>/dev/null)"

# stop_worker -- the worker, and the listener it forked
WPID=
stop_worker() {
	[ -n "$WPID" ] || return 0
	# shellcheck disable=SC2046  # one pid per word
	stop_process "$WPID" $(ps -A -o pid= -o ppid= 2>/dev/null | awk -v p="$WPID" '$2 == p { print $1 }')
	WPID=
}
register_cleanup stop_worker

# connects ADDR PORT -- a TCP connection to ADDR:PORT is taken, within 2s
connects() { XYMON_TIMEOUT=2 "$XYMONCLIENT" "$1:$2" "status probe.conn green probe" >/dev/null 2>&1; }

# listening ADDR PORT -- wait for the worker to take connections on ADDR:PORT
listening() {
	for _ in {1..100}; do
		connects "$1" "$2" && return 0
		kill -0 "$WPID" 2>/dev/null || return 1
		sleep 0.1
	done
	return 1
}

# worker ARGS... -- xymond_rrd in network mode, in the background; WPID
worker() {
	"$XYMONCMD" --env="$work/xymonserver.cfg" "$XYMOND_RRD" --rrddir="$work/rrd" \
		--locator="127.0.0.1:$lport" "$@" >"$work/logs/rrd.log" 2>&1 &
	WPID=$!
}

# An IPv4 short form: listens, and registers under it
# The control: a worker on every address, reached at 127.0.0.2 or not
worker --listen=":$p2" --locatorid="127.0.0.1:$p2"
listening 127.0.0.1 "$p2" || fail "the worker did not listen on every address with --listen=:$p2: $(cat "$work/logs/rrd.log")"
wildcard_reached=no
connects 127.0.0.2 "$p2" && wildcard_reached=yes
stop_worker

worker --listen="127.1:$p1"
listening 127.0.0.1 "$p1" || fail "the worker did not listen on 127.0.0.1:$p1 with --listen=127.1:$p1: $(cat "$work/logs/rrd.log")"
partial=
if [ "$wildcard_reached" = yes ]; then
	! connects 127.0.0.2 "$p1" || fail "with --listen=127.1:$p1 the worker listens on every address, not 127.0.0.1"
else
	partial="that it bound 127.0.0.1 alone: 127.0.0.2 does not reach a listener on every address here"
fi
stop_worker

# A name, where /etc/hosts makes localhost 127.0.0.1
if grep -qE '^[[:space:]]*127\.0\.0\.1[[:space:]](.*[[:space:]])?localhost([[:space:]]|$)' /etc/hosts 2>/dev/null; then
	worker --listen="localhost:$p3"
	listening 127.0.0.1 "$p3" || fail "the worker did not listen on 127.0.0.1:$p3 with --listen=localhost:$p3: $(cat "$work/logs/rrd.log")"
	stop_worker
else
	partial="${partial:+$partial; }a name: /etc/hosts does not map localhost to 127.0.0.1"
fi

# Malformed: a message and a plain exit, never a signal
for args in "--listen=[::1]junk --locatorid=127.0.0.1:$p1" "--listen=127.0.0.1 --locatorid=127.0.0.1:$p1" \
	   "--listen=127.0.0.1:$p1 --locatorid=127.0.0.1:junk"; do
	# shellcheck disable=SC2086
	worker $args
	rc=0
	for _ in {1..100}; do kill -0 "$WPID" 2>/dev/null || break; sleep 0.1; done
	if kill -0 "$WPID" 2>/dev/null; then stop_worker; fail "the worker kept running with $args: $(cat "$work/logs/rrd.log")"; fi
	wait "$WPID" || rc=$?
	[ "$rc" -ge 1 ] && [ "$rc" -le 127 ] || fail "the worker ended with rc $rc on $args: $(cat "$work/logs/rrd.log")"
	case $args in
	--listen=127.0.0.1:*) want='Invalid --locatorid=127.0.0.1:junk' ;;
	*) want='Invalid --listen=' ;;
	esac
	assert_contains "$want" "$(cat "$work/logs/rrd.log")" "the worker refused $args without saying why"
done

claim="a network worker listens on 127.1:PORT as before and on a name, and refuses a malformed --listen or --locatorid with a message"
if [ -n "$partial" ]; then pass_partial "$claim" "$partial"; else pass "$claim"; fi
