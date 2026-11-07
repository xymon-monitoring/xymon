#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/xymonnet/hostscfg-url-other-name.sh
#
# xymonnet names a URL test on a line with an address only when the URL is
# fetched from a different address than the line's -- the one case that will
# change meaning when #516 fetches URL tests from the line's address.
#
# Today such a URL is fetched from its own host's address (an IP literal as
# written, or whatever its name resolves to); the line's address plays no part.
# A future release fetches it from the line's address, so a line whose URL is
# already fetched from that same address will not change and is not named. The
# name case is decided from the address the URL actually resolved to -- which
# xymonnet looked up to fetch it anyway, at no extra lookup -- so a name that
# resolves to the line's own address is not named. A URL pinned with "=IP", a
# proxied URL, and a URL for the line's own name keep their target and are not
# named. "localhost" is used because it resolves to 127.0.0.1 everywhere.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin XYMONNET xymonnet/xymonnet

work=$(mktempdir)
mkdir -p "$work/home/etc" "$work/home/tmp"

# run HOSTSLINE -- xymonnet's --report output for a hosts.cfg of that one line
run() {
	printf '%s\n' "$1" >"$work/home/etc/hosts.cfg"
	XYMONHOME="$work/home" XYMONTMP="$work/home/tmp" MACHINE=probe \
		"$XYMONNET" --no-update --noping --report --timeout=2 2>&1 || true
}

# A name that resolves to a DIFFERENT address than the line's -- the real case.
out=$(run '127.0.0.2 web # http://localhost/')
assert_contains "host web: the URL http://localhost/ is fetched from 127.0.0.1, the address localhost resolves to" "$out" \
	"a URL whose name resolves elsewhere than the line's address was not named"
assert_contains "fetch it from 127.0.0.2" "$out" \
	"the warning did not name the line's own address"

# A content check, same shape, is named too.
out=$(run '127.0.0.2 web # cont;http://localhost/;ok')
assert_contains "the URL http://localhost/ is fetched from 127.0.0.1" "$out" \
	"a content check whose name resolves elsewhere was not named"

# Credentials never reach the warning (the URL is printed in canonical form).
out=$(run '127.0.0.2 web # http://user:secret@localhost/')
assert_contains "the URL http://localhost/ is fetched from 127.0.0.1" "$out" \
	"a URL with credentials, resolving elsewhere, was not named"
assert_not_contains "secret" "$out" "the warning printed the URL's password"

# An IP-literal URL host that differs from the line's address: named, and
# without "resolves to" -- an address does not resolve.
out=$(run '192.0.2.91 juan # http://198.51.100.5/')
assert_contains "the URL http://198.51.100.5/ is fetched from 198.51.100.5" "$out" \
	"an IP-literal URL host differing from the line's address was not named"
assert_contains "fetch it from 192.0.2.91" "$out" \
	"the IP-literal warning did not name the line's own address"
assert_not_contains "resolves to" "$out" "the IP-literal warning said \"resolves to\""

# The line's own address is read as written in hosts.cfg, not a value a
# co-resident test (here ssh) may have overwritten with the line name's DNS
# result: the warning must name column 1 (127.0.0.2), not where localhost
# resolves (127.0.0.1).
out=$(run '127.0.0.2 localhost # ssh http://198.51.100.5/')
assert_contains "fetch it from 127.0.0.2" "$out" \
	"the warning named a DNS-resolved address instead of the hosts.cfg column 1"
assert_not_contains "fetch it from 127.0.0.1" "$out" \
	"the line name's DNS result leaked into the warning as the line's address"

# Not named -- each already fetches from the line's address, or keeps its own:
#   name resolving TO the line's address; IP-literal host equal to it;
#   the line's own name; a "=IP"-pinned URL; a 0.0.0.0 line.
for line in \
	'127.0.0.1 web # http://localhost/' \
	'127.0.0.1 web # http://127.1/' \
	'198.51.100.5 web # http://198.51.100.5/' \
	'127.0.0.2 web # http://web/' \
	'127.0.0.2 web # http://localhost=127.0.0.1/' \
	'0.0.0.0 web # http://localhost/' ; do
	out=$(run "$line")
	assert_not_contains "is fetched from" "$out" "\"$line\" was named, but its URL does not change target"
done

pass "xymonnet names a URL test only when it is fetched from a different address than the line's, not a name resolving to the line's address, an IP equal to it, the line's own name, a =IP pin, or a 0.0.0.0 line"
