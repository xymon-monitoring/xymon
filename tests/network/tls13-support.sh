#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/network/tls13-support.sh
#
# Guard for the "TLSv1.3-only" xymonnet test option added by
# xymon-monitoring/xymon#33 (the `httpsd://` scheme / SSLVERSION_TLS13).
#
# The wiring spans:
#   - xymonnet/contest.h : the SSLVERSION_TLS13 selector,
#   - xymonnet/contest.c : pinning the SSL context to TLS 1.3
#                          (SSL_CTX_set_{min,max}_proto_version + TLS1_3_VERSION),
#   - xymonnet/httptest.c: mapping the `httpsd://` scheme to it.
#
# Sibling of alpn-support.sh: a behavioural run needs a TLS 1.3 server, the
# build CI already compiles these files, so this is a static guard that the
# wiring survives future edits. Skips only when the source files are absent
# (e.g. an autopkgtest run with no source tree); if a present tree has lost the
# TLS 1.3 wiring, that is a regression and the test fails rather than skips.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
HDR="$ROOT/xymonnet/contest.h"
SSL="$ROOT/xymonnet/contest.c"
HTTP="$ROOT/xymonnet/httptest.c"

for f in "$HDR" "$SSL" "$HTTP"; do
	[ -f "$f" ] || skip "$(basename "$f") absent"
done

hdr=$(cat "$HDR")

# These source files ship in the same tree as this test, so missing TLS 1.3
# wiring means someone removed it -- a regression, not a pre-feature tree --
# and must fail, never skip green. The only legitimate skip is an absent
# *environment* (the source files themselves not present, handled above), not
# absent project code. Assert every layer unconditionally.

assert_contains "SSLVERSION_TLS13" "$hdr" \
	"contest.h lost the SSLVERSION_TLS13 selector (#33)"

# The proto-version pinning is only effective if it is REACHED from the
# SSLVERSION_TLS13 case: deleting the case label strands the set_*_proto_version
# lines as dead code, and an inserted `break` right after the label makes them
# unreachable -- both leave the lines in the file, so whole-file greps stay a
# false green. Extract just the case body (from the label up to and including
# its first `break;`) and assert the pins live INSIDE it. A missing label gives
# an empty body; a break-after-label gives a body with no pins; either fails.
tls13_case=$(awk '/case SSLVERSION_TLS13:/{c=1} c{print} c&&/break;/{exit}' "$SSL")
[ -n "$tls13_case" ] || fail \
	"contest.c no longer has the SSLVERSION_TLS13 switch case -- TLS 1.3 setup unreachable (#33)"

# Within that case body, "TLS 1.3-only" needs BOTH the minimum and the maximum
# pinned to TLS1_3_VERSION: a minimum alone still lets OpenSSL negotiate a
# higher version, defeating the intent. Pin to TLS1_3_VERSION specifically so
# the 1.2/1.1/1.0 cases' calls cannot satisfy this, and scope to the case body
# so removing or unreaching either line must fail here.
grep -q 'set_min_proto_version(.*TLS1_3_VERSION)' <<<"$tls13_case" \
	|| fail "contest.c no longer pins the SSL minimum to TLS 1.3 inside the TLS13 case (#33)"
grep -q 'set_max_proto_version(.*TLS1_3_VERSION)' <<<"$tls13_case" \
	|| fail "contest.c no longer caps the SSL maximum at TLS 1.3 inside the TLS13 case (#33)"

# The scheme mapping is the line that actually binds the httpsd:// scheme to
# TLS 1.3: httptest.c selects the version from the scheme option char, "d" ->
# SSLVERSION_TLS13. A bare grep for the token is a false green -- the selector
# survives any mutation of the option char ("d" -> "x") or of the assignment
# target, leaving the scheme unmapped while the word stays. Pin the assertion
# to the option char AND the version on the same mapping line.
grep -Eq 'strstr\(.*schemeopts, *"d"\).*SSLVERSION_TLS13' "$HTTP" \
	|| fail "httptest.c no longer maps the httpsd:// scheme char (\"d\") to TLS 1.3 (#33)"

exit 0
