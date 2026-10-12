#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/xymonnet/netsvcs-builtin-name.sh
#
# A name in XYMONNETSVCS that is also one of xymonnet's built-in tests leaves
# the built-in test in place.
#
# XYMONNETSVCS was loaded first, as plain TCP tests, and the built-in test of
# the same name then got that TCP test back: with "http" listed, an http URL
# was reported twice, once by its own test and once by the TCP code reading
# the http test's private data as its own, past the end of it; with "ntp"
# listed, xymonnet aborted (exit 134) and reported nothing.
#
# Checked with xymonnet --no-update, which prints the statuses it would send:
#   - an http URL, with "http" in XYMONNETSVCS, gets one http status, and
#     not the TCP test's "Service http on testhost is not OK";
#   - the ntp tag, with "ntp" in XYMONNETSVCS, gets its ntp status.
# Neither test needs to reach anything: the statuses are counted, not their
# colour.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin XYMONNET xymonnet/xymonnet

work=$(mktempdir)
mkdir -p "$work/home/etc" "$work/tmp"
cp "$(find_root)/xymonnet/protocols.cfg" "$work/home/etc/protocols.cfg"

# statuses HOSTLINE -- run xymonnet on one hosts.cfg line, with smtp, http and
# ntp in XYMONNETSVCS, and print what it would send.
statuses() {
	printf '%s\n' "$1" > "$work/home/etc/hosts.cfg"
	(cd "$work" && env XYMONHOME="$work/home" XYMONTMP="$work/tmp" XYMONNETSVCS="smtp http ntp" \
		"$XYMONNET" --noping --no-update --timeout=3) 2>"$work/err" || :
}

out=$(statuses '127.0.0.1 testhost # testip http://127.0.0.1:1/')
assert_equal "1" "$(printf '%s\n' "$out" | grep -c '^ *status[^ ]* testhost\.http ' || :)" \
	"an http URL with http in XYMONNETSVCS did not get exactly one http status: $out $(tail -3 "$work/err")"
case "$out" in
*"Service http on testhost"*) fail "an http URL with http in XYMONNETSVCS was reported by the TCP test as well: $out" ;;
esac

out=$(statuses '127.0.0.1 testhost # testip ntp')
assert_equal "1" "$(printf '%s\n' "$out" | grep -c '^ *status[^ ]* testhost\.ntp ' || :)" \
	"the ntp tag with ntp in XYMONNETSVCS did not get its ntp status: $out $(tail -3 "$work/err")"

pass "a built-in test named in XYMONNETSVCS stays the built-in test: one http status for an http URL, and the ntp status for the ntp tag"
