#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/client/msgcache-version.sh
#
# msgcache --version names msgcache. It printed "xymonproxy version", a line
# copied from xymonproxy.c, so the answer looked like another program's.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin MSGCACHE client/msgcache

out=$("$MSGCACHE" --version)
assert_match '^msgcache version ' "$out" "msgcache --version names msgcache"
pass "msgcache --version names msgcache"
