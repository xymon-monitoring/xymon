#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/buildsystem/variant-products.sh
#
# variant_products() (tests/lib/assert.sh) names, for each build, where each
# program a test asks for lives. A name missing from it keeps the caller's
# default path in every build: XYMONCLIENT, the xymon client under the name
# the xymond tests and free_port() use, was missing, so a test in an area the
# client builds run looked for common/xymon there and skipped, and the
# coverage floor failed the client lanes.
#
# Checked on the table itself, so it needs no build: XYMONCLIENT resolves as
# XYMON does in each build, a program every build makes resolves in each, and
# a server-only one resolves nowhere else.
set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

# expect VARIANT NAME WANTED -- product_path VARIANT NAME prints WANTED.
expect() {
	local got
	got=$(product_path "$1" "$2")
	[ "$got" = "$3" ] || fail "in a $1 build, $2 resolves to '$got', not '$3'"
}

for v in server localclient client; do
	expect "$v" XYMONCLIENT "$(product_path "$v" XYMON)"
done
expect server      XYMONCLIENT common/xymon
expect localclient XYMONCLIENT client/xymon
expect client      XYMONCLIENT client/xymon

expect server      XYMONCMD common/xymoncmd
expect client      XYMONCMD client/xymoncmd
expect client      LOGFETCH client/logfetch
expect localclient XYMOND_CLIENT client/xymond_client
expect client      XYMOND_CLIENT ""
expect server      XYMOND xymond/xymond
expect client      XYMOND ""

pass "variant_products() resolves XYMONCLIENT as XYMON in every build, and each program where its build puts it"
