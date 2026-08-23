#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/buildsystem/client-shared-regions.sh
#
# Several functions are copied into more than one client script rather than
# sourced (build/mkclientshared.sh says why). Each copy sits in a
# "BEGIN SHARED <name>" region stamped from client/shared/<name>.sh, and the
# result is committed. This test regenerates a copy of the tree and fails on
# any difference: a region edited by hand, or a fragment edited without
# regenerating.
#
# Copy-based, so the source tree is never written, and it works without .git.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)

[ -f "$ROOT/build/mkclientshared.sh" ] || fail "build/mkclientshared.sh is missing"

work=$(mktempdir)
mkdir -p "$work/build" "$work/client"
cp "$ROOT/build/mkclientshared.sh" "$work/build/"
cp -R "$ROOT/client/shared" "$work/client/"
cp "$ROOT"/client/xymonclient-*.sh "$work/client/"

sh "$work/build/mkclientshared.sh" >"$work/gen.log" 2>&1 \
	|| { cat "$work/gen.log" >&2; fail "build/mkclientshared.sh failed"; }

for client in "$work"/client/xymonclient-*.sh; do
	name=$(basename "$client")
	cmp -s "$client" "$ROOT/client/$name" || {
		diff "$ROOT/client/$name" "$client" >&2 || true
		fail "client/$name does not match client/shared/: regenerate with build/mkclientshared.sh"
	}
done

pass "every shared region in the client scripts matches its fragment in client/shared/"
