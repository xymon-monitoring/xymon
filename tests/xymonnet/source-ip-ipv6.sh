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
#   127.0.0.1 ntp host, ntp@2001:db8::99  the other family: not used for the
#                                     probe, the default is, as a TCP test
#   (the ntp cases only where an IPv4 "@" source reaches the probe's bind)
#
# The IPv6-host cases need ::1: on a host with no IPv6, xymonnet's
# socket(AF_INET6) fails before any bind is tried, so the warning these cases
# look for cannot appear. Probe for ::1 first and skip those cases (PARTIAL)
# where it is absent.

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

# Does this host have the IPv6 loopback (::1)? The ::1-host cases need it (see
# the header); where it is absent they are skipped.
if have_ipv6_loopback; then have_v6=1; else have_v6=; fi
partial=
[ -n "$have_v6" ] || partial="a host with no IPv6 loopback (::1): the IPv6-host cases were skipped"

out=$(run "127.0.0.1 v4host # testip demoport@192.0.2.99")
assert_contains "Could not bind to source IP 192.0.2.99" "$out" "an IPv4 test no longer binds its IPv4 source address"

if [ -n "$have_v6" ]; then
	out=$(run "::1 v6host # testip demoport" --source-ip=2001:db8::99)
	assert_not_contains "Invalid source ip" "$out" "--source-ip refused an IPv6 address"
	assert_contains "Could not bind to source IP 2001:db8::99" "$out" "a test of an IPv6 host did not bind the IPv6 --source-ip"

	out=$(run "::1 v6host # testip demoport@2001:db8::99")
	assert_contains "Could not bind to source IP 2001:db8::99" "$out" "a test of an IPv6 host did not bind its \"@\" IPv6 source address"

	out=$(run "::1 v6host # testip demoport@127.0.0.1")
	assert_not_contains "source IP" "$out" "an IPv4 source address was used, or refused, for a test of an IPv6 host"

	out=$(run "::1 v6host # testip demoport@::ffff:192.0.2.99")
	assert_not_contains "source IP" "$out" "an IPv4-mapped source address was used, or refused, for a test of an IPv6 host"

	out=$(run "::1 v6host # testip demoport" --source-ip=::1)
	assert_not_contains "source IP" "$out" "binding the IPv6 source address ::1, which this host has, failed"
fi

# IPv4 short forms, as inet_aton() read them
out=$(run "127.0.0.1 v4host # testip demoport@127.1" --source-ip=127.1)
assert_not_contains "Invalid source ip" "$out" "--source-ip refused the IPv4 short form 127.1"
assert_not_contains "Invalid source IP" "$out" "a test's \"@\" source refused the IPv4 short form 127.1"

# The built-in ntp probe: a source of the other family than the target -- the
# --source-ip= default and a test's own "@" alike -- is not used; the default
# is, as for a TCP test. Only where the probe gets as far as the bind: it looks
# the "ntp" service up, which a minimal system's /etc/services may not have, so
# an IPv4 source this host does not have must fail to bind first.
out=$(NTPDATE="" run "127.0.0.1 v4host # testip ntp@192.0.2.99")
if grep -q "bind to source address failed" <<<"$out"; then
	out=$(NTPDATE="" run "127.0.0.1 v4host # testip ntp" --source-ip=2001:db8::99)
	assert_not_contains "source address invalid" "$out" "an IPv6 --source-ip was used for the ntp probe of an IPv4 host"
	# An IPv4-mapped source reaches the probe as the IPv4 address
	out=$(NTPDATE="" run "127.0.0.1 v4host # testip ntp@::ffff:192.0.2.99")
	assert_contains "bind to source address failed" "$out" "an IPv4-mapped \"@\" source did not reach the ntp probe as its IPv4 address"
	# A test's own "@" source of the other family is not used either: the
	# default is, as for a TCP test, so the probe is not refused.
	out=$(NTPDATE="" run "127.0.0.1 v4host # testip ntp@2001:db8::99")
	assert_not_contains "source address invalid" "$out" "a test's own IPv6 \"@\" source was used for the ntp probe of an IPv4 host instead of the default"
	ntp_partial=
else
	ntp_partial="the ntp probe: it does not reach the bind here (no \"ntp\" service?)"
fi

[ -z "$ntp_partial" ] || partial="${partial:+$partial; }$ntp_partial"

claim="xymonnet takes an IPv6 source address from --source-ip and \"@\", binds it for IPv6 tests, and keeps an IPv4 one, short forms included, for IPv4 tests"
if [ -n "$partial" ]; then pass_partial "$claim" "$partial"; else pass "$claim"; fi
