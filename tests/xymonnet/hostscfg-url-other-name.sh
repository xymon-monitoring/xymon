#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/xymonnet/hostscfg-url-other-name.sh
#
# xymonnet says when a URL test on a line with an address is for another name.
#
# Today such a URL is fetched from the address its own name resolves to, and
# the line's address plays no part. #516 proposes fetching it from the line's
# address, and ships a warning release first: each URL whose host is not the
# line's name, on a line whose hosts.cfg address is not 0.0.0.0, is named in
# the "Warning output" of xymonnet's own status. A URL pinned with "=IP" or
# sent through a proxy keeps its own target, so it is not named, and neither
# is a URL for the line's own name, in any case.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin XYMONNET xymonnet/xymonnet

work=$(mktempdir)
mkdir -p "$work/home/etc" "$work/home/tmp"

# run HOSTSLINE -- xymonnet's output for a hosts.cfg of that one line
run() {
	printf '%s\n' "$1" >"$work/home/etc/hosts.cfg"
	XYMONHOME="$work/home" XYMONTMP="$work/home/tmp" MACHINE=probe \
		"$XYMONNET" --no-update --noping --report --timeout=2 2>&1 || true
}

out=$(run '127.0.0.1 web # http://localhost/')
assert_contains "host web: the URL http://localhost/ is fetched from the address localhost resolves to. A future release will fetch it from 127.0.0.1" "$out" \
	"a URL for another name on an addressed line was not named"

out=$(run '127.0.0.1 web # cont;http://localhost/;ok')
assert_contains "the URL http://localhost/ is fetched from" "$out" \
	"a content check's URL for another name was not named"

out=$(run '127.0.0.1 web # http://user:secret@localhost/')
assert_contains "the URL http://localhost/ is fetched from" "$out" \
	"a URL with credentials for another name was not named"
assert_not_contains "secret" "$out" "the warning printed the URL's password"

for line in '127.0.0.1 web # http://web/' '127.0.0.1 web # http://WEB/' \
	'127.0.0.1 web # http://localhost=127.0.0.1/' '0.0.0.0 web # http://localhost/'; do
	out=$(run "$line")
	assert_not_contains "is fetched from" "$out" "\"$line\" was named, but its URL keeps its target"
done

pass "xymonnet names a URL test for another name on a line with an address, and not one for the line's name, pinned with =IP, or on a 0.0.0.0 line"
