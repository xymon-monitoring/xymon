#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/buildsystem/bump-dev.sh
#
# After a release, build/bump-dev.sh sets include/version.h to the next
# X.Y.Z-dev, which the release workflow then proposes as a pull request
# (#676). Run on a copy of include/version.h: the release's number becomes
# the next -dev; a version.h that already moved on is left alone; nothing
# but the VERSION line changes; a version that is not X.Y.Z is refused.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
work=$(mktempdir)
mkdir -p "$work/include"

seed() {  # seed VERSION -- a version.h like the tree's, naming VERSION
	sed -e "s/^#define VERSION \".*\"\$/#define VERSION \"$1\"/" \
		"$ROOT/include/version.h" >"$work/include/version.h"
}
run() { (cd "$work" && sh "$ROOT/build/bump-dev.sh" "$@"); }
line() { grep '^#define VERSION "' "$work/include/version.h"; }

seed 4.3.32
out=$(run 4.3.32) || fail "bump-dev.sh failed after a release: $out"
assert_equal '#define VERSION "4.3.33-dev"' "$(line)" "after 4.3.32"
assert_equal '#define VERSION "4.3.33-dev"' "$out" "what it prints"

seed 4.3.32
before=$(grep -v '^#define VERSION "' "$work/include/version.h")
run 4.3.32 >/dev/null
assert_equal "$before" "$(grep -v '^#define VERSION "' "$work/include/version.h")" "the rest of version.h"

seed 4.3.9
run 4.3.9 >/dev/null
assert_equal '#define VERSION "4.3.10-dev"' "$(line)" "a two-digit patch level"

seed 4.3.33-dev
run 4.3.32 >/dev/null || fail "bump-dev.sh failed on a version.h that had moved on"
assert_equal '#define VERSION "4.3.33-dev"' "$(line)" "already bumped, left alone"

seed 4.4.0
run 4.3.32 >/dev/null
assert_equal '#define VERSION "4.4.0"' "$(line)" "past another release, left alone"

seed 4.3.32
for bad in "" 4.3 4.3.32.1 4.3.x rel-4.3.32 4.3.32-rc1 .4.3 4..3; do
	if run "$bad" >/dev/null 2>&1; then
		fail "bump-dev.sh accepted '$bad' as a version"
	fi
done
assert_equal '#define VERSION "4.3.32"' "$(line)" "a refused version changes nothing"

pass "build/bump-dev.sh sets the next X.Y.Z-dev after a release, and only then"
