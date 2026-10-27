#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/network/sni-support.sh
#
# Guard for the xymonnet SNI support: `options sni` on a protocols.cfg entry
# sends the tested hostname as the TLS server name.
#
# Without it, a host serving several names on one address answers a handshake
# that carries no server name with its DEFAULT certificate -- so the expiry
# reported in the sslcert column belongs to whichever certificate that address
# answers with, not to the name the test is for. It is wrong quietly, which is
# the worst way for a monitoring system to be wrong.
#
# The wiring spans three layers:
#   - lib/netservices.h : the TCP_SNI flag,
#   - lib/netservices.c : parsing the `sni` option into the service flags,
#   - xymonnet/xymonnet.c: handing the tested hostname to the test, which
#     setup_ssl() then passes to SSL_set_tlsext_host_name().
#
# Exercising it for real needs a TLS server holding two certificates and
# selecting on the server name; the build CI already compiles these files, so
# this is a static guard that the wiring across the layers (and the manpage)
# survives future edits -- the same tradeoff tests/network/alpn-support.sh
# makes for ALPN. Skips only when the source files are absent (e.g. an
# autopkgtest run against an installed package with no source tree); a present
# tree that has lost the wiring is a regression and fails rather than skipping.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
HDR="$ROOT/lib/netservices.h"
SRC="$ROOT/lib/netservices.c"
NET="$ROOT/xymonnet/xymonnet.c"
SSL="$ROOT/xymonnet/contest.c"
MAN="$ROOT/xymonnet/protocols.cfg.5"

for f in "$HDR" "$SRC" "$NET" "$SSL" "$MAN"; do
	[ -f "$f" ] || skip "$(basename "$f") absent"
done

# header: the flag the option sets
assert_contains "TCP_SNI" "$(cat "$HDR")" "netservices.h lost the TCP_SNI flag"

# parser: the bare `sni` option must set that flag. Pin the assignment rather
# than grepping for the word: "sni" appears in comments and in the manpage
# reference, so a bare match stays green after the parsing is deleted.
grep -Eq '"sni"\)[[:space:]]*==[[:space:]]*0\)[[:space:]]*first->rec->flags[[:space:]]*\|=[[:space:]]*TCP_SNI' "$SRC" \
	|| fail "netservices.c no longer parses the 'sni' option into TCP_SNI"

# xymonnet: the flag must actually put the tested hostname on the test.
# Assigning anything else -- or assigning unconditionally -- is the bug this
# guards, so pin both the flag test and the hostname being the value stored.
grep -Eq 'flags[[:space:]]*&[[:space:]]*TCP_SNI' "$NET" \
	|| fail "xymonnet.c no longer checks TCP_SNI before setting the server name"
grep -Eq 'tt->sni[[:space:]]*=[[:space:]]*t->host->hostname' "$NET" \
	|| fail "xymonnet.c no longer hands the tested hostname to the handshake"

# RFC 6066: an address literal is not a legal server_name, and a server that
# enforces it aborts the handshake. Sending one would turn a cosmetic gap into
# an outage, so the guard against it is part of the contract, not a nicety.
grep -Eq 'sni_name_is_address' "$NET" \
	|| fail "xymonnet.c no longer refuses to send an address literal as the server name"

# contest.c: the stored name must still reach OpenSSL.
grep -Eq 'SSL_set_tlsext_host_name\(item->ssldata, item->sni\)' "$SSL" \
	|| fail "contest.c no longer passes the server name to OpenSSL"

# the manpage documents the option
assert_contains "sni - send the tested hostname" "$(cat "$MAN")" \
	"protocols.cfg.5 does not document the sni option"

pass "options sni sends the tested hostname as the TLS server name, and never an address literal"
