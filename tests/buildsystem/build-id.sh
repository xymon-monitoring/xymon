#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Bruno Manzoni
#
# tests/buildsystem/build-id.sh
#
# build/build-id.sh names the commit a build that is not a release comes
# from, and nothing for a release (#676). Run on a copy of the script in a
# scratch repository: no git, a release tag, a commit after it, a dirty tree,
# no tag, a shallow clone that cannot reach the tag, the override, a tree
# inside someone else's repository, and an unchanged identity leaving the file
# alone. Then, when the tree is built, xymon --build-info reports what the
# build wrote, xymon --version does not (the client update level falls back to
# it, so a leak there would update every client on every commit), and the
# shipped page footers carry the identity as &XYMONBUILD.

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

# A shallow clone cannot reach the tag a few commits back, so git describe
# falls to --always: a bare hash, which still names the commit. The report
# this issue is about is an empty identity, never a hash. A clone that cannot
# be made leaves the case unchecked, and the test says so rather than passing.
gap=""
if git clone -q --depth 1 --no-tags "file://$work/src" "$work/shallow" 2>/dev/null; then
	mkdir -p "$work/shallow/build" && cp "$ROOT/build/build-id.sh" "$work/shallow/build/"
	sid=$(cd "$work/shallow" && unset XYMON_BUILD_ID && sh build/build-id.sh && cat build/build-id)
	assert_match '^[0-9a-f]{7,}$' "$sid" "a shallow clone with no tag reachable: the hash"
else
	gap="git clone --depth 1 failed here, so the shallow-clone case was not checked"
fi

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

# The commit reaches the page footer: the shipped footers name &XYMONBUILD,
# which headfoot expands from the environment -- empty for a release, the
# commit otherwise. A release build compiles XYMONBUILD empty, so the footer
# shows only the number there.
for footer in stdnormal_footer critical_footer; do
	assert_contains "&XYMONBUILD" "$(cat "$ROOT/xymond/webfiles/$footer")" \
		"$footer carries the build identity"
done

xymon=$ROOT/common/xymon
if [ ! -x "$xymon" ] || [ ! -f "$ROOT/build/build-id" ]; then
	pass_partial "build/build-id.sh" "xymon not built, --build-info not checked${gap:+; $gap}"
fi
want=$(cat "$ROOT/build/build-id")
got=$(env -u XYMONBUILD "$xymon" --build-info)
assert_equal "version=$(sed -n 's/^#define VERSION "\(.*\)"$/\1/p' "$ROOT/include/version.h")
build=$want" "$got" "xymon --build-info"

# The release number alone on a surface a program compares: the client update
# level falls back to xymon --version (client/xymonclient.sh), so the identity
# must not reach it, or every client updates on every commit.
plain=$(sed -n 's/^#define VERSION "\(.*\)"$/\1/p' "$ROOT/include/version.h")
assert_equal "Xymon version $plain" "$(XYMONBUILD=leak-me "$xymon" --version)" \
	"xymon --version stays the plain number with XYMONBUILD set"

[ -z "$gap" ] || pass_partial "build/build-id.sh, and xymon --build-info reports it" "$gap"
pass "build/build-id.sh, and xymon --build-info reports it"
