#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/network/sni-tcp-tls.sh
#
# Plain TCP-TLS probes (imaps, smtps, pop3s, nntps, ...) must be able to send a
# TLS servername (SNI), so a peer that routes TLS by SNI -- common with hosted
# or multi-tenant mail -- answers instead of failing the handshake. SNI used to
# be wired for HTTP tests only; this guards that the plain TCP path wires it the
# same way: on by default sending the host name, --sni to set the global
# default, per-host nosni to disable, and "sni=NAME" to send an explicit name.
#
# Static guard, like its siblings alpn-support.sh / tls13-support.sh: a
# behavioural run needs a TLS peer that reports the servername it was handed,
# which the suite does not stand up for any TLS feature (verified by hand
# against such a peer instead). To avoid a whole-file grep going green on a
# stranded or misplaced line, the servername wiring is asserted INSIDE the
# extracted SNI-setting block, not just somewhere in the file.
set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
n="$ROOT/xymonnet/xymonnet.c"
h="$ROOT/xymonnet/xymonnet.h"
c="$ROOT/xymonnet/contest.c"
w="$ROOT/xymonnet/httptest.c"
l="$ROOT/lib/loadhosts.c"
[ -f "$n" ] && [ -f "$h" ] && [ -f "$c" ] && [ -f "$w" ] && [ -f "$l" ] || skip "xymonnet sources not in this checkout"

# Strip comment-only lines so the block's own comments don't satisfy the rules,
# and capture into a variable: `... | grep -q` would let grep -q exit early and
# SIGPIPE the upstream grep, which set -o pipefail then reports as failure.
ncode=$(grep -vE '^[[:space:]]*(\*|/\*|//)' "$n" || true)
hcode=$(grep -vE '^[[:space:]]*(\*|/\*|//)' "$h" || true)
ccode=$(grep -vE '^[[:space:]]*(\*|/\*|//)' "$c" || true)
wcode=$(grep -vE '^[[:space:]]*(\*|/\*|//)' "$w" || true)
lcode=$(grep -vE '^[[:space:]]*(\*|/\*|//)' "$l" || true)
has() { grep -qE -- "$1" <<<"$2"; }   # here-string (no pipe -> no SIGPIPE); -- so a pattern starting with '-' is not read as options

has 'int[[:space:]]+sni;' "$hcode" \
	|| fail "testedhost_t carries no sni decision"

has 'XMH_FLAG_SNI.*->sni = 1' "$ncode" \
	|| fail "the per-host 'sni' flag is not read onto the host"
has 'XMH_FLAG_NOSNI.*->sni = -1' "$ncode" \
	|| fail "the per-host 'nosni' flag is not read onto the host"

# Extract the SNI-setting block: from the TCP_SSL gate to where its brace
# depth returns to zero. Asserting the servername wiring lives in HERE (not
# just anywhere in the file) means a line moved out of the reachable path, or
# left as dead code, fails instead of passing on a whole-file match.
block=$(awk '
	/flags & TCP_SSL/ { inblk = 1 }
	inblk {
		print
		o = gsub(/{/, "{"); c = gsub(/}/, "}"); depth += o - c
		if (opened && depth <= 0) exit
		if (o > 0) opened = 1
	}
' "$n")
[ -n "$block" ] || fail "could not locate the TCP_SSL-gated SNI-setting block in xymonnet.c"

has 'flags & TCP_SSL' "$block" \
	|| fail "SNI for TCP tests is not gated on TCP_SSL"
has 'host->hostname' "$block" \
	|| fail "the hostname is never used as the SNI servername (inside the TCP_SSL block)"
has 'tt->sni = name' "$block" \
	|| fail "the chosen name is never handed to the TCP-TLS test as SNI"
has 'snienabled' "$block" \
	|| fail "the global --sni default is not consulted inside the TCP_SSL block"
has 'inet_pton' "$block" \
	|| fail "an IP-literal name is not excluded from SNI (RFC 6066 forbids an address as servername)"
has 'snienabled = 1' "$ccode" \
	|| fail "SNI is not enabled by default (snienabled should default to 1)"

# The default is shared, so enabling it also enables the HTTP path -- which
# must apply the same IP-literal guard, or an https://<ip>/ test would put an
# address in SNI (RFC 6066 forbids it) the moment the default flipped on.
has 'inet_pton' "$wcode" \
	|| fail "the HTTP SNI path (httptest.c) does not exclude an IP-literal URL host"
has 'tcptest->sni = host' "$wcode" \
	|| fail "the HTTP SNI path no longer assigns the guarded URL host"

# Explicit "sni=NAME" override. The IP-literal guard above applies to it too:
# the name feeds the same guarded assignment, so "sni=1.2.3.4" is not sent.
has 'XMH_SNINAME\][[:space:]]*=[[:space:]]*"sni="' "$lcode" \
	|| fail "the 'sni=' host tag (XMH_SNINAME) is not registered in loadhosts.c"
has 'char[[:space:]]*\*sniname;' "$hcode" \
	|| fail "testedhost_t has no sniname field for the explicit SNI name"
has 'XMH_SNINAME' "$ncode" \
	|| fail "the 'sni=NAME' override is not read onto the host"
has 'host->sniname' "$block" \
	|| fail "the explicit sniname is never used as the servername (inside the TCP_SSL block)"
has 'sni=%s is an IP' "$ncode" \
	|| fail "an explicit sni=<ip> is dropped silently instead of being logged"

pass "plain TCP-TLS tests wire SNI: host field, sni/nosni flags, in-block TCP_SSL gate + hostname + default, no IP-literal SNI (TCP and HTTP), sni=NAME override"
