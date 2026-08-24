#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/lib/xymond-daemon.sh
#
# Starting a real xymond on a loopback port, for the tests that need one.
#
# Three tests carried a byte-identical copy of this, and the logic is subtle
# enough that three copies is how one of them drifts: the port has to be
# chosen without the kernel handing it to someone else first, and that is not
# something any probe can guarantee.
#
# Source after tests/lib/assert.sh. The caller provides $XYMOND, $XYMONCLIENT
# (require_bin), $work (a scratch directory holding xymond.log), and defines
# xymond_launch(); start_xymond() drives it and exports PORT and XYMOND_PID.

# ephemeral_port_floor -- where the kernel starts handing out ephemeral ports,
# or nothing when this kernel does not say. Purely an optimization input: see
# free_port().
#
# Asked by knob rather than by OS: the names do not overlap between the BSDs
# (FreeBSD and macOS spell it portrange.first, OpenBSD portfirst, NetBSD
# anonportmin), so whichever one answers identifies the kernel by itself and
# no uname test is needed. /sbin/sysctl as well as sysctl, since NetBSD does
# not put it on a non-root PATH.
ephemeral_port_floor() {
	local v knob s

	v=$(cut -f1 /proc/sys/net/ipv4/ip_local_port_range 2>/dev/null || true)
	case "$v" in ''|*[!0-9]*) ;; *) printf '%s' "$v"; return 0 ;; esac

	for knob in net.inet.ip.portrange.first net.inet.ip.portfirst net.inet.ip.anonportmin; do
		for s in sysctl /sbin/sysctl; do
			v=$("$s" -n "$knob" 2>/dev/null || true)
			case "$v" in ''|*[!0-9]*) continue ;; esac
			printf '%s' "$v"
			return 0
		done
	done
	return 1
}

# free_port -- a loopback port nothing answers a Xymon ping on.
#
# This does NOT make the port safe to bind, and cannot: the probe only sees a
# listener that answers a ping, so a socket held by anything else reads as
# free, and the port can be taken in the window before the caller binds it
# anyway. Correctness lives in start_xymond's bind retry.
#
# What this does is make a collision rare, by drawing from just below the
# kernel's ephemeral range where the kernel will say where that starts. Where
# it will not, or where there is no unprivileged room below it, no window is
# invented -- an invented one is how this went wrong before: assuming 32768
# puts every draw *inside* the ephemeral range on OpenBSD (starts at 1024) and
# FreeBSD (10000). The historical span is used instead and the retry carries it.
free_port() {
	local p tries=0 lo hi

	hi=$(ephemeral_port_floor || true)
	case "$hi" in ''|*[!0-9]*) hi= ;; esac
	if [ -n "$hi" ] && [ "$hi" -gt 9216 ]; then
		lo=$(( hi - 8192 ))
	else
		# No usable window below the range: draw from the historical span and
		# let the bind retry deal with a collision.
		lo=20000
		hi=40000
	fi

	while [ "$tries" -lt 50 ]; do
		p=$(( lo + (RANDOM % (hi - lo)) ))
		"$XYMONCLIENT" "127.0.0.1:$p" "ping" >/dev/null 2>&1 || { printf '%s' "$p"; return 0; }
		tries=$((tries+1))
	done
	return 1
}

# start_xymond [extra xymond arguments] -- pick a port, hand it to the test's
# xymond_launch(), and wait for the daemon to answer. Sets PORT and XYMOND_PID.
#
# A port that turned out to be taken is the one startup failure worth retrying,
# and the budget is per call: a script that starts the daemon a dozen times must
# not arrive at its last startup with the retries already spent. Every other
# failure fails at once, so a xymond that cannot bind anywhere still fails
# instead of looping.
start_xymond() {
	local attempt=0 i

	while :; do
		PORT=$(free_port) || fail "no free port for xymond"
		xymond_launch "$PORT" "$@"

		# An answered ping only counts while our own child is alive: the probe
		# cannot tell this xymond from any other Xymon-speaking listener that
		# holds the port, and a dead child with a stranger on its port would
		# otherwise read as a successful startup.
		i=0
		while [ "$i" -lt 100 ]; do
			"$XYMONCLIENT" "127.0.0.1:$PORT" "ping" >/dev/null 2>&1 &&
				kill -0 "$XYMOND_PID" 2>/dev/null && return 0
			kill -0 "$XYMOND_PID" 2>/dev/null || break
			sleep 0.1
			i=$((i+1))
		done

		kill -0 "$XYMOND_PID" 2>/dev/null && break
		grep -q 'Cannot bind to listen socket' "$work/xymond.log" 2>/dev/null || break
		[ "$attempt" -lt 5 ] || break
		attempt=$((attempt+1))
	done

	cat "$work/xymond.log" >&2
	kill -0 "$XYMOND_PID" 2>/dev/null &&
		fail "xymond did not answer on 127.0.0.1:$PORT" ||
		fail "xymond exited during startup"
}
