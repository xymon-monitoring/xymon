#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/xymonnet/scheme-unknown-no-warning.sh
#
# A "scheme://name" test names a TLS service and the servername to check its
# certificate against. A scheme that IS a xymonnet service but does no TLS
# (ssh://) is a misconfiguration and is logged. A scheme that is not a xymonnet
# service at all (http6s://, a tag an external test owns) is left to that test
# and must NOT be logged: warning about it every poll turned a working
# xymonnet yellow. This is issue #957, a regression from sending SNI for every
# TLS service -- before that the "scheme://" branch did not exist, so an
# unknown scheme reached no warning.
#
# Checked by reading a hosts.cfg that carries both a known non-TLS scheme and
# an unknown one, and reading what xymonnet logs while it sets the tests up (no
# network needed -- the message is emitted during config parsing). Dropping the
# NULL-service case fails the second assertion.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin XYMONNET xymonnet/xymonnet

root=$(find_root)
work=$(mktempdir); register_cleanup "rm -rf '$work'"
mkdir -p "$work/home/etc" "$work/tmp"
cp "$root/xymonnet/protocols.cfg" "$work/home/etc/protocols.cfg"
# HTTP6s:// is the real-world tag: an external http6 add-on names its column
# with an upper-case scheme, which does not match the lower-case "http" test
# prefix and so reaches the "scheme://" branch (issue #957).
printf '127.0.0.1\ttesthost\t# conn ssh://example.com HTTP6s://example.com\n' \
	> "$work/home/etc/hosts.cfg"

out=$(XYMONHOME="$work/home" XYMONTMP="$work/tmp" \
	"$XYMONNET" --no-update --noping --dns=ip --timeout=1 2>&1 || :)

# A known service that does no TLS is still reported.
grep -qF "'ssh://' is not a TLS service" <<<"$out" \
	|| fail "a known non-TLS scheme (ssh://) should still be reported:
$out"

# An unknown scheme is an external test's tag, not a misconfiguration.
if grep -qF "'HTTP6s://' is not a TLS service" <<<"$out"; then
	fail "an unknown scheme (HTTP6s://) must not be reported -- it is left to its external test (#957):
$out"
fi

pass "a scheme that is no xymonnet service is left to its external test unlogged, while a known non-TLS scheme is still reported"
