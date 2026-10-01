#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# build/pin-check.sh tells a test that pins its fix from one that does not.
#
# Against a throwaway repository, not this one: the check reverts files and
# rebuilds, and in a configured tree that would be the tree under test. Each
# branch there carries one change to src/value and one test, and --local is
# asked about it:
#
#   pinned    the test reads the value the fix writes   -> ok
#   blind     the test passes whatever the value is     -> unguarded
#   control   the same test, declaring itself a control -> declared control
#   adapted   an existing test the branch only edits     -> reported apart
#
# Then the two rules that read a dependency, which the integration run needs
# and which can be asked without GitHub: which "Depends-on: #N" lines count,
# and where the range removed for a stacked branch starts.
set -eu
. "$(dirname "$0")/../lib/assert.sh"
ROOT=$(find_root)

command -v git >/dev/null 2>&1 || skip "git is needed to build the throwaway repository"
command -v make >/dev/null 2>&1 || skip "make is needed: the check rebuilds after each revert"

work=$(mktempdir); register_cleanup "rm -rf '$work'"
repo=$work/repo

# --local asks GitHub which pull request a commit heads when gh is there. Not
# here: a gh that always fails keeps this test off the network and on the
# path where only --after names a dependency.
mkdir -p "$work/bin"
printf '#!/bin/sh\nexit 1\n' >"$work/bin/gh"
chmod +x "$work/bin/gh"
PATH=$work/bin:$PATH
export PATH

g() { git -C "$repo" -c user.name=t -c user.email=t@t -c commit.gpgsign=false "$@"; }

mkdir -p "$repo/build" "$repo/src" "$repo/tests/unit"
cp "$ROOT/build/pin-check.sh" "$repo/build/pin-check.sh"
printf 'all:\n\t@:\n' >"$repo/Makefile"
printf 'broken\n' >"$repo/src/value"
printf '#!/bin/sh\ntest -f src/value\n' >"$repo/tests/unit/existing.sh"
chmod +x "$repo/tests/unit/existing.sh"
git init -q "$repo"
g symbolic-ref HEAD refs/heads/main
g add -A
g commit -q -m base

# branch NAME TESTBODY -- a branch off main fixing src/value, with one test.
branch() {
	g checkout -q -b "$1" main
	printf 'fixed\n' >"$repo/src/value"
	mkdir -p "$repo/tests/unit"
	printf '#!/bin/sh\n%s\n' "$2" >"$repo/tests/unit/$1.sh"
	chmod +x "$repo/tests/unit/$1.sh"
	g add -A
	g commit -q -m "$1"
}

branch pinned  'grep -qx fixed src/value'
branch blind   'test -f src/value'
branch control '# control: passes with and without the fix
test -f src/value'

# adapted: the branch fixes src/value and edits an existing test without
# making it depend on the fix, the way a change adds a variable to a test it
# would otherwise break.
g checkout -q -b adapted main
printf 'fixed\n' >"$repo/src/value"
printf '# kept running under the change\n' >>"$repo/tests/unit/existing.sh"
g add -A
g commit -q -m adapted

# check NAME -- the report --local gives on that branch.
check() {
	g checkout -q "$1"
	( cd "$repo" && BASE_BRANCH=main sh build/pin-check.sh --local 2>&1 ) || :
}

out=$(check pinned)
assert_contains "1 test/fix pair checked out sound." "$out" \
	"a test reading what its fix writes is reported sound"
assert_not_contains "### Unguarded" "$out" \
	"a test reading what its fix writes is not reported unguarded"

out=$(check blind)
assert_contains "### Unguarded (1)" "$out" \
	"a test that passes without its fix is reported unguarded"
assert_contains "tests/unit/blind.sh" "$out" \
	"the unguarded report names the test"

out=$(check control)
assert_contains "### Declared controls (1)" "$out" \
	"a test declaring '# control:' is reported as a control"
assert_not_contains "### Unguarded" "$out" \
	"a declared control is not reported unguarded"

out=$(check adapted)
assert_contains "### Passing without the change, which only edited them (1)" "$out" \
	"an existing test the branch only edits is reported apart"
assert_contains "tests/unit/existing.sh" "$out" \
	"the edited test is named"
assert_not_contains "### Unguarded" "$out" \
	"an edited existing test is not reported unguarded"

# The working tree is left as it was found: the fix put back, the test intact.
assert_equal "fixed" "$(cat "$repo/src/value")" \
	"the check restores the fix it removed"
assert_equal "" "$(g status --porcelain --untracked-files=no)" \
	"the check leaves no tracked file changed"

# --local on a stacked branch: without --after its range carries the
# dependency's fix and test as well; --after DEP leaves only its own. A
# dependency the branch does not contain is reported, not applied.
g checkout -q -b lower main
printf 'fixed\n' >"$repo/src/value"
mkdir -p "$repo/tests/unit"
printf '#!/bin/sh\ngrep -qx fixed src/value\n' >"$repo/tests/unit/lower.sh"
chmod +x "$repo/tests/unit/lower.sh"
g add -A; g commit -q -m lower
g checkout -q -b upper lower
printf 'fixed\n' >"$repo/src/other"
printf '#!/bin/sh\ngrep -qx fixed src/other\n' >"$repo/tests/unit/upper.sh"
chmod +x "$repo/tests/unit/upper.sh"
g add -A; g commit -q -m upper
g checkout -q -b aside main
printf 'x\n' >"$repo/src/aside"; g add -A; g commit -q -m aside
g checkout -q upper

out=$( cd "$repo" && BASE_BRANCH=main sh build/pin-check.sh --local 2>&1 ) || :
assert_contains "2 test/fix pairs checked out sound." "$out" \
	"without --after a stacked branch is checked with its dependency's test"
out=$( cd "$repo" && BASE_BRANCH=main sh build/pin-check.sh --local --after lower 2>&1 ) || :
assert_contains "1 test/fix pair checked out sound." "$out" \
	"--after DEP checks a stacked branch on its own commits only"
out=$( cd "$repo" && BASE_BRANCH=main sh build/pin-check.sh --local --after aside 2>&1 ) || :
assert_contains "its history does not contain aside" "$out" \
	"a dependency the branch does not contain is reported"
assert_contains "2 test/fix pairs checked out sound." "$out" \
	"a dependency the branch does not contain leaves the range at the fork point"
g checkout -q main

# Which declarations count: a line holding only "Depends-on: #N", CRLF or not,
# outside a fence. Prose, a line with more on it, and inline code do not.
deps=$(printf 'Depends-on: #12\r\nas #9 says, Depends-on: #3\nDepends-on: #4 once it lands\nsee `Depends-on: #5`\n```\nDepends-on: #6\n```\nDepends-on: #7\n' \
	| sh "$repo/build/pin-check.sh" --declared-deps | tr '\n' ' ')
assert_equal "12 7 " "$deps" "only whole-line declarations outside a fence count"

# Where the range for a stacked branch starts: after a dependency its history
# contains, and at the fork point when it does not contain it.
g checkout -q -b dep main
printf 'dep\n' >"$repo/src/dep"; g add -A; g commit -q -m dep
g checkout -q -b top dep
printf 'top\n' >"$repo/src/top"; g add -A; g commit -q -m top
g checkout -q -b elsewhere main
printf 'x\n' >"$repo/src/x"; g add -A; g commit -q -m elsewhere

fork=$(g rev-parse main)
got=$( cd "$repo" && sh build/pin-check.sh --range-base "$fork" top dep 2>/dev/null )
assert_equal "$(g rev-parse dep)" "$got" \
	"a contained dependency moves the start of the range to its head"

got=$( cd "$repo" && sh build/pin-check.sh --range-base "$fork" top elsewhere 2>"$work/missed" )
assert_equal "$fork" "$got" \
	"a dependency the history does not contain leaves the range at the fork point"
assert_equal "elsewhere" "$(cat "$work/missed")" \
	"the dependency that could not narrow the range is named"

# The integration merges carry their own identity: a CI runner has none, and
# a merge git refuses for that reads exactly like a conflict. Here no identity
# reaches git at all -- an empty HOME, no system or XDG configuration.
g checkout -q -b m1 main
printf 'm1\n' >"$repo/src/m1"; g add -A; g commit -q -m m1
g checkout -q -b m2 main
printf 'm2\n' >"$repo/src/m2"; g add -A; g commit -q -m m2
g checkout -q -b clash main
printf 'clash\n' >"$repo/src/value"; g add -A; g commit -q -m clash
g checkout -q -b moved main
printf 'moved\n' >"$repo/src/value"; g add -A; g commit -q -m moved
g checkout -q main
mkdir -p "$work/nohome"
noid() { ( cd "$repo" && HOME=$work/nohome XDG_CONFIG_HOME=$work/nohome \
	GIT_CONFIG_NOSYSTEM=1 sh build/pin-check.sh "$@" 2>&1 ); }

got=$(noid --integrate main m1 m2 | tr '\n' ' ')
assert_equal "m1 m2 " "$got" \
	"the integration merges succeed with no git identity configured"

out=$(noid --integrate moved clash) && fail "a set of which nothing merges did not stop the run"
assert_contains "none of the 1 pull requests merges onto moved" "$out" \
	"a set of which nothing merges stops the run"
assert_contains "CONFLICT" "$out" \
	"the stop quotes what git said about the first refusal"

pass "pinned, unguarded, adapted and control told apart; dependencies read and applied; integration merges need no identity"
