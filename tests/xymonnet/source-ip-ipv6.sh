#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/xymonnet/source-ip-ipv6.sh
#
# xymonnet's source address, --source-ip= or a test's "@ADDRESS", may be
# IPv6 (#116). It used to be read as IPv4 only: an IPv6 --source-ip was
# refused, and a test of an IPv6 host never bound its source address.
#
# Most source addresses here are documentation addresses this host does not
# have, so binding one fails, and the warning shows the bind was tried; no
# listener is needed. ::1, which the host has, must bind without a warning,
# which a wrong address length would give.
#
#   --source-ip=2001:db8::99          accepted, not "Invalid source ip"
#   ::1 host, demoport@2001:db8::99   bound: "Could not bind to source IP"
#   ::1 host, --source-ip=::1         bound, no warning (where ::1 exists)
#   ::1 host, demoport@127.0.0.1      not bound: the other family
#   ::1 host, demoport@::ffff:192.0.2.99  the IPv4 address it stands for:
#                                     not bound, the other family
#   127.0.0.1 host, demoport@192.0.2.99  still bound, as before
#   --source-ip=127.1, demoport@127.1    IPv4 short forms, accepted as before
#   127.0.0.1 ntp host, --source-ip=2001:db8::99  the default of the other
#                                     family is not used for the probe
#   127.0.0.1 ntp host, ntp@::ffff:192.0.2.99  reaches the probe's bind as
#                                     the IPv4 address
#   127.0.0.1 ntp host, ntp@2001:db8::99  fails the probe, as before
#   (the ntp cases only where an IPv4 "@" source reaches the probe's bind)

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin XYMONNET xymonnet/xymonnet

work=$(mktempdir)
mkdir -p "$work/home/etc" "$work/home/tmp"
# Port 1: nothing listens there, and the connection only has to be tried
printf '[demoport]\n   port 1\n' >"$work/home/etc/protocols.cfg"

# run HOSTSLINE [OPTION...] -- xymonnet's output for a hosts.cfg of that line
run() {
	printf '%s\n' "$1" >"$work/home/etc/hosts.cfg"
	shift
	XYMONHOME="$work/home" XYMONTMP="$work/home/tmp" MACHINE=probe \
		"$XYMONNET" --no-update --noping --report --timeout=3 "$@" 2>&1 || true
}

out=$(run "127.0.0.1 v4host # testip demoport@192.0.2.99")
assert_contains "Could not bind to source IP 192.0.2.99" "$out" "an IPv4 test no longer binds its IPv4 source address"

out=$(run "::1 v6host # testip demoport" --source-ip=2001:db8::99)
assert_not_contains "Invalid source ip" "$out" "--source-ip refused an IPv6 address"
assert_contains "Could not bind to source IP 2001:db8::99" "$out" "a test of an IPv6 host did not bind the IPv6 --source-ip"

out=$(run "::1 v6host # testip demoport@2001:db8::99")
assert_contains "Could not bind to source IP 2001:db8::99" "$out" "a test of an IPv6 host did not bind its \"@\" IPv6 source address"

out=$(run "::1 v6host # testip demoport@127.0.0.1")
assert_not_contains "source IP" "$out" "an IPv4 source address was used, or refused, for a test of an IPv6 host"

out=$(run "::1 v6host # testip demoport@::ffff:192.0.2.99")
assert_not_contains "source IP" "$out" "an IPv4-mapped source address was used, or refused, for a test of an IPv6 host"

# IPv4 short forms, as inet_aton() read them
out=$(run "127.0.0.1 v4host # testip demoport@127.1" --source-ip=127.1)
assert_not_contains "Invalid source ip" "$out" "--source-ip refused the IPv4 short form 127.1"
assert_not_contains "Invalid source IP" "$out" "a test's \"@\" source refused the IPv4 short form 127.1"

# The built-in ntp probe: an IPv6 default source is not used for an IPv4
# server, while a test's own "@" source of the other family fails its probe.
# Only where the probe gets as far as the bind: it looks the "ntp" service
# up, which a minimal system's /etc/services may not have, so an IPv4 source
# this host does not have must fail to bind first.
out=$(NTPDATE="" run "127.0.0.1 v4host # testip ntp@192.0.2.99")
if grep -q "bind to source address failed" <<<"$out"; then
	out=$(NTPDATE="" run "127.0.0.1 v4host # testip ntp" --source-ip=2001:db8::99)
	assert_not_contains "source address invalid" "$out" "an IPv6 --source-ip was used for the ntp probe of an IPv4 host"
	# An IPv4-mapped source reaches the probe as the IPv4 address
	out=$(NTPDATE="" run "127.0.0.1 v4host # testip ntp@::ffff:192.0.2.99")
	assert_contains "bind to source address failed" "$out" "an IPv4-mapped \"@\" source did not reach the ntp probe as its IPv4 address"
	out=$(NTPDATE="" run "127.0.0.1 v4host # testip ntp@2001:db8::99")
	assert_contains "source address invalid" "$out" "a test's own IPv6 \"@\" source was not refused for the ntp probe of an IPv4 host"
	ntp_partial=
else
	ntp_partial="the ntp probe: it does not reach the bind here (no \"ntp\" service?)"
fi

# The host has ::1, read from its interface list, as tests/lib/assert.sh's
# have_ipv6_loopback() does on the branches that have it
ifs=$({ ip -6 addr show 2>/dev/null || true; ifconfig -a 2>/dev/null || true; })
partial=$ntp_partial
if grep -qE 'inet6 (addr: ?)?::1([/ %]|$)' <<<"$ifs"; then
	out=$(run "::1 v6host # testip demoport" --source-ip=::1)
	assert_not_contains "source IP" "$out" "binding the IPv6 source address ::1, which this host has, failed"
else
	partial="${partial:+$partial; }a successful IPv6 bind: this host has no ::1"
fi

claim="xymonnet takes an IPv6 source address from --source-ip and \"@\", binds it for IPv6 tests, and keeps an IPv4 one, short forms included, for IPv4 tests"
if [ -n "$partial" ]; then pass_partial "$claim" "$partial"; else pass "$claim"; fi
