#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/buildsystem/build-id.sh
#
# build/build-id.sh names the commit a build that is not a release comes
# from, and nothing for a release (#676). Run on a copy of the script in a
# scratch repository: no git, a release tag, a commit after it, a dirty tree,
# no tag, the override, a tree inside someone else's repository, and an
# unchanged identity leaving the file alone. Then, when the tree is built,
# xymon --build-info reports what the build wrote.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
command -v git >/dev/null 2>&1 || skip "git not installed"
git -C / --version >/dev/null 2>&1 \
	|| skip "$(git --version) has no -C, which this test reaches its throwaway repository with (needs git 1.8.5 or later)"

work=$(mktempdir)
mkdir -p "$work/src/build"
cp "$ROOT/build/build-id.sh" "$work/src/build/"
g() { git -C "$work/src" -c user.name=t -c user.email=t@t "$@" >/dev/null 2>&1; }
id() { (cd "$work/src" && unset XYMON_BUILD_ID && sh build/build-id.sh) && cat "$work/src/build/build-id"; }

assert_equal "" "$(id)" "not a git checkout"

g init -q && echo a >"$work/src/f" && g add f build/build-id.sh && g commit -q -m one
assert_match '^[0-9a-f]{7,}$' "$(id)" "no release tag: the hash"

g tag rel-9.9.9
assert_equal "" "$(id)" "clean checkout of a release tag"

echo b >>"$work/src/f"
assert_equal "9.9.9-dirty" "$(id)" "release tag with changes"

g commit -q -am two
assert_match '^9\.9\.9-1-g[0-9a-f]{7,}$' "$(id)" "a commit after the tag"

old=$(id)
touch -t 200001010000 "$work/src/build/build-id"
assert_equal "$old" "$(id)" "same identity again"
[ "$(find "$work/src/build/build-id" -newermt 2000-01-02 | wc -l)" -eq 0 ] \
	|| fail "an unchanged identity rewrote build/build-id"

out=$(cd "$work/src" && XYMON_BUILD_ID='pkg-1.0"; rm -rf /' sh build/build-id.sh && cat build/build-id)
assert_equal "pkg-1.0rm-rf" "$out" "the override, kept to safe characters"
out=$(cd "$work/src" && XYMON_BUILD_ID= sh build/build-id.sh && cat build/build-id)
assert_equal "" "$out" "an empty override"

# A release tarball unpacked inside a packaging repository is still a release
mkdir -p "$work/src/sub/build" && cp "$ROOT/build/build-id.sh" "$work/src/sub/build/"
out=$(cd "$work/src/sub" && unset XYMON_BUILD_ID && sh build/build-id.sh && cat build/build-id)
assert_equal "" "$out" "a tree inside another repository"

xymon=$ROOT/common/xymon
if [ ! -x "$xymon" ] || [ ! -f "$ROOT/build/build-id" ]; then
	pass_partial "build/build-id.sh" "xymon not built, --build-info not checked"
fi
want=$(cat "$ROOT/build/build-id")
got=$(env -u XYMONBUILD "$xymon" --build-info)
assert_equal "version=$(sed -n 's/^#define VERSION "\(.*\)"$/\1/p' "$ROOT/include/version.h")
build=$want" "$got" "xymon --build-info"
pass "build/build-id.sh, and xymon --build-info reports it"
