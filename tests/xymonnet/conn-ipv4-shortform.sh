#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/xymonnet/conn-ipv4-shortform.sh
#
# A URL host written as an IPv4 short form is tested, not failed as a DNS
# error (xymonnet/contest.c).
#
# add_tcp_test() resolved its already-numeric target with
# getaddrinfo(AI_NUMERICHOST), which on OpenBSD reads only a dotted quad: a URL
# host written as the short form 127.1 failed with a DNS error there. Now an
# IPv4 address is read with inet_aton() first. The URL carries 127.1 as the
# test target; the host line keeps a valid 0.0.0.0 address.
#
# Only OpenBSD's resolver rejects the short form, so this reddens there alone.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin XYMONNET xymonnet/xymonnet

work=$(mktempdir)
mkdir -p "$work/home/etc" "$work/home/tmp"

printf '0.0.0.0\tlocalhost\t# http://127.1:1/\n' >"$work/home/etc/hosts.cfg"
out=$(XYMONHOME="$work/home" XYMONTMP="$work/home/tmp" MACHINE=probe \
	"$XYMONNET" --no-update --ping --report --timeout=3 --debug 2>&1 || true)

assert_contains "Adding tcp test IP=127.1, port=1" "$out" "the URL host 127.1 was not tested"
assert_not_contains "DNS lookup failed for 127.1" "$out" "the URL host 127.1 was looked up as a name"
assert_not_contains "DNS error" "$out" "the URL host 127.1 gave a DNS error"

pass "a URL host written as the IPv4 short form 127.1 is tested, not failed as a DNS error"
