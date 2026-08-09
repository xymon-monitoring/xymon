#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/web/svcstatus-scriptname.sh
#
# Regression guard for svcstatus crashing when SCRIPT_NAME is unset
# (commit f01d07dfa).
#
# parse_query() built the client URI from getenv("SCRIPT_NAME") and ran
# strlen() on it unchecked. A web server always sets SCRIPT_NAME, but any
# other invocation -- a shell, a test harness, a misconfigured wrapper --
# left it NULL and svcstatus segfaulted in parse_query() before emitting a
# single line. The fix substitutes "" for a missing SCRIPT_NAME.
#
# This drives the built svcstatus.cgi with SCRIPT_NAME removed from the
# environment and a minimal status request, and asserts it does NOT die from
# a signal. parse_query() runs before any host lookup, so no Xymon server is
# needed: past the fix the request simply fails later (unknown host, exit 1);
# without the fix it is killed by SIGSEGV (exit 139) inside parse_query().
# The discriminator is therefore "terminated by a signal" (exit >= 128), not
# a specific non-zero code.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin SVCSTATUS_CGI web/svcstatus.cgi

work=$(mktempdir)

# Wrap in `timeout` when available so a stray local xymond that keeps the
# connection open makes the test time out rather than stall the suite. The
# request is minimal but well-formed enough to reach the FRM_STATUS branch
# that touches SCRIPT_NAME; the crash (or its absence) is decided in
# parse_query(), before the host is ever looked up.
runner=("$SVCSTATUS_CGI")
command -v timeout >/dev/null 2>&1 && runner=(timeout 30 "$SVCSTATUS_CGI")

set +e
env -u SCRIPT_NAME \
	REQUEST_METHOD=GET \
	QUERY_STRING="HOST=nosuchhost.example&SERVICE=conn" \
	XYMONHOME="$work" XYMONHISTLOGS="$work" \
	"${runner[@]}" >/dev/null 2>&1
rc=$?
set -e

# 124 = timeout fired (environment problem, not the bug under test).
[ "$rc" -ne 124 ] || skip "svcstatus.cgi did not return within the time limit (environment)"

# The bug manifests as termination by a signal: exit code >= 128. SIGSEGV
# gives 139. A graceful failure (unknown host with no server) is < 128.
if [ "$rc" -ge 128 ]; then
	sig=$((rc - 128))
	fail "svcstatus.cgi was killed by signal $sig with SCRIPT_NAME unset -- parse_query() crash regressed (f01d07dfa)"
fi

pass "svcstatus.cgi survives a missing SCRIPT_NAME (exit $rc, no signal)"
