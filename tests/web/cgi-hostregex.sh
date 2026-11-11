#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/web/cgi-hostregex.sh
#
# Compiles and runs cgi-hostregex-harness.c against the real lib/cgi.c and
# lib/matching.c (via libxymon.a). cgi_hostregex() is what the CGIs put
# between "host=^" and "$" in a xymondboard query (#758): it must match the
# host name it is given and no other -- "^web01.example.com$" also matched
# "web01-example.com" -- and refuse anything that is not a host name.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
. "$(dirname "$0")/../lib/build-harness.sh"

ROOT=$(find_root)

require_c_buildenv "$ROOT"

work=$(mktempdir)

build_xymon_libs "$ROOT" "$work/libbuild.log" libxymon.a

build_harness "$work" "$work/harness" \
	"$(dirname "$0")/cgi-hostregex-harness.c" libxymon.a

"$work/harness" >"$work/run.log" 2>&1 \
	|| { cat "$work/run.log" >&2; fail "cgi_hostregex() does not make a host name a literal pattern"; }

pass "cgi_hostregex(): a host name becomes a pattern that matches that name only, and anything else is refused"
