#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
#
# build/pin-check.sh -- find tests that no longer prove the fix they were
# written for.
#
# A test earns its place by failing when its fix is absent. Every author checks
# that once, against the main of the day -- and that is not the question,
# because main moves. A change merged later can leave an older test passing
# whatever happens to the code it was guarding, and nothing says so: the suite
# stays green, the test stays listed, and the fix is unprotected.
#
# The shape is narrow enough to state. A pull request that adds a *guard*
# invalidates every test that proves a bug by its damage. Xymon hit it once so
# far, and it is why this exists: #509 proves an empty XYMSRV by showing that
# "trimhistory --drop" then deletes a listed host's history, and #508 adds a
# refusal to drop on an empty host list. Each is right. Merge both and the files
# survive either way, so #509's test passed with its own fix reverted and said
# nothing about the defect it was written for.
#
# So ask it again, against the main we are going to have:
#
#     on a tree carrying every open pull request, does this one's test still
#     fail when this one's fix is removed?
#
# That is deliberately not a question about pairs. Asking "does #508 blind
# #509" is 2,415 combinations at seventy open pull requests; asking the above is
# seventy reverts, because every interaction is already present in the ground
# being stood on. The pair case surfaces on its own, as a revert that will not
# apply.
#
# Usage:
#     build/pin-check.sh --pr N       one pull request, whatever its state. Open,
#                                     and it is checked on an integration branch
#                                     of every open one; merged, and it is
#                                     checked on the tree it landed on; closed
#                                     without merging, and it says so and stops
#     build/pin-check.sh --local      the branch in front of you; with gh, the
#                                     dependencies its open pull request
#                                     declares are taken out of its range, and
#                                     --after REF (repeatable) names one by hand
#     build/pin-check.sh              every open pull request
#     build/pin-check.sh --commit M   a change that is not a pull request: one
#                                     commit (M^..M), a merge commit (M^1..M^2),
#                                     or an explicit A..B range
#     build/pin-check.sh --against R  ask the question on R instead of where the
#                                     change landed -- "--pr 223 --against main"
#                                     is "does it still pin its fix today"
#
# Environment: REPO (default xymon-monitoring/xymon), BASE_BRANCH (default main),
# JOBS (default: processors online), TEST_TIMEOUT (default 300s per test).
#
# Needs a configured tree, and gh authenticated for everything but --local. The
# modes that check out another commit, and --local, revert files in the working
# tree, so they refuse to start on a dirty one. Do not "git clean -fd" around
# this: Makefile and include/config.h are untracked in a configured tree, and
# removing them unconfigures it -- every test needing a binary then skips, which
# reads as a fix correctly pinned.
#
# --local is what an author wants before pushing, and it is the cheap half of
# the answer: it says the test fails without its fix *today*, which is the check
# every author already makes and which #509 passed while being blind. Only the
# integration run asks the other half. It reverts files in the working tree, so
# it refuses to start on a dirty one.
#
# Outcomes:
#     ok              the test fails without the fix. Nothing to do.
#     FLAG            it passes, and the change added it. The fix is unguarded.
#     modified        it passes, but the change only edited an existing test --
#                     often to keep it running under the change, not to prove
#                     it. Reported apart, for a reader to check the diff.
#     control         it passes and says it is meant to (see below).
#     cannot verify   the revert would not apply, because something else
#                     rewrote the same lines. The pair case; needs a person.
#     no usable test  it does not pass on the intact tree either, so its
#                     behaviour without the fix means nothing.
#
# Some tests are written to pass with and without the fix, on purpose: they pin
# the behaviour a fix must NOT change. Such a test declares itself in its own
# header and is reported apart from the findings:
#
#     # control: passes with and without the fix
#
# tests/README.md carries the convention, alongside "# native-primitive: NAME",
# which the portability checker already requires for the same reason: declare
# the intent, or a checker is entitled to assume the worst of a test that
# cannot fail.
#
# Three things this does NOT do, each of which cost a wrong answer while the
# method was being worked out:
#
#   - it never reverts a file a pull request ADDS under tests/. A helper a test
#     needs to exist at all is scaffolding, not the fix; reverting an add
#     deletes it, the test dies at "source", and that is indistinguishable from
#     a test failing correctly. It hid the one real finding outright. A
#     *modified* helper or runner is reverted, because it may well be the fix;
#     tests/lib/xymond-daemon.sh and tests/testsuite both are, in open pull
#     requests today.
#   - it never trusts a test it has not seen pass. Every test runs on the intact
#     tree first. Without that, a test that cannot run at all -- a missing
#     binary, an absent dependency -- counts as a fix correctly pinned, and the
#     report looks like broad coverage while proving almost nothing. Which is
#     this very failure, one level up.
#   - it does not decide whether a large pull request's tests belong to its fix.
#     A branch carrying three features touches tests belonging to all three. It
#     is reported as it measures, and left to a reader.
#
# A pull request built on another carries that one's commits, and reverting
# them would take out a fix that is not its own. So the range removed for a
# pull request starts after every dependency it declares with a
# "Depends-on: #N" line -- the rule CONTRIBUTING.md sets and the dependency
# labels use -- when that dependency's head is part of its history. A
# dependency it does not contain (rebased since, or built on a branch that has
# closed) cannot narrow the range, and the report says so.
#
# Shell and awk rather than a richer language, for the reason the test suite
# gives: python and perl are not on the BSD lanes, and --local is meant to run
# on a contributor's own machine.

set -u

REPO=${REPO:-xymon-monitoring/xymon}
BASE=${BASE_BRANCH:-main}
JOBS=${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)}
TEST_TIMEOUT=${TEST_TIMEOUT:-300}

mode=all
only=
commit=
against=
subject=
after=

while [ $# -gt 0 ]; do
	case $1 in
		--local) mode=local ;;
		--after)
			shift
			[ -n "${1:-}" ] || { echo "--after wants a commit or branch" >&2; exit 2; }
			after="$after $1" ;;
		--pr)
			shift
			case ${1:-} in
				'' | *[!0-9]*) echo "--pr wants a pull-request number" >&2; exit 2 ;;
			esac
			mode=one; only=$1 ;;
		--commit)
			shift
			[ -n "${1:-}" ] || { echo "--commit wants a commit or A..B range" >&2; exit 2; }
			mode=commit; commit=$1 ;;
		--against)
			shift
			[ -n "${1:-}" ] || { echo "--against wants a commit or ref" >&2; exit 2; }
			against=$1 ;;
		# For tests/buildsystem/pin-check.sh only: the two rules that read a
		# dependency, without GitHub.
		--declared-deps) mode=declared ;;
		--range-base) shift; mode=rangebase; break ;;
		-h | --help)
			# the whole header block, however it is ordered: from line 3
			# until the first line that is not a comment
			awk 'NR < 3 { next } /^#/ { sub(/^# ?/, ""); print; next } { exit }' "$0"
			exit 0 ;;
		*) echo "unknown argument: $1" >&2; exit 2 ;;
	esac
	shift
done

root=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd) || exit 2
cd "$root" || exit 2

work=$(mktemp -d "${TMPDIR:-/tmp}/pin-check.XXXXXX") || exit 2
trap 'rm -rf "$work"' 0 1 2 15
: >"$work/narrow"

# A hanging test would otherwise sit until the runner's own ceiling. timeout(1)
# is not everywhere, and where it is missing a hang is the same problem the
# suite already has, so this degrades rather than refusing to run.
if command -v timeout >/dev/null 2>&1; then
	tmo="timeout $TEST_TIMEOUT"
else
	tmo=
fi

die() { echo "$*" >&2; exit 1; }

# build_tree -- the tree as it currently stands. Run after every revert and
# every restore: a test that execs a daemon would otherwise run the binary built
# from the code we have just put back, and a fix absent from the source but
# present in the binary produces a finding that is purely an artefact.
build_tree() { make -j"$JOBS" >"$work/make.log" 2>&1; }

# is_inert PATH -- true when reverting the path cannot change what a local test
# run does, so a pull request made only of these is skipped rather than
# reported as a wall of flags.
is_inert() {
	case $1 in
		.github/* | docs/* | *.md | Changes | RELEASENOTES | \
		*.1 | *.2 | *.3 | *.4 | *.5 | *.6 | *.7 | *.8) return 0 ;;
	esac
	return 1
}

# is_control FILE -- the test declares that it passes either way.
is_control() {
	head -n 40 "$1" 2>/dev/null | grep -q '^#[ 	]*control:'
}

# run_test FILE -- the suite's contract: 0 pass, 77 skip, else fail.
run_test() {
	$tmo bash "$1" >"$work/test.log" 2>&1
}

# changed MB REF -- the paths one unit of work touches.
changed() { git diff --name-only "$1..$2"; }

# declared_deps -- the pull requests a description on stdin declares it
# depends on: lines holding nothing but "Depends-on: #N", outside a fenced
# block. CR is dropped first, so a description saved with CRLF reads the same.
declared_deps() {
	tr -d '\r' | awk '
		/^(```|~~~)/ { fence = !fence; next }
		fence { next }
		/^Depends-on:[ \t]*#[0-9]+[ \t]*$/ {
			sub(/^Depends-on:[ \t]*#/, ""); sub(/[ \t]*$/, ""); print
		}'
}

# range_base MB REF DEP... -- where the range removed for REF starts: MB, or
# the latest declared dependency head that REF's history contains. A dependency
# head it does not contain is named on stderr and left out.
range_base() {
	rb=$1 rb_ref=$2
	shift 2
	for rb_dep in "$@"; do
		if git merge-base --is-ancestor "$rb_dep" "$rb_ref" 2>/dev/null; then
			# The later of the two: a dependency already inside rb adds nothing.
			if git merge-base --is-ancestor "$rb" "$rb_dep" 2>/dev/null; then
				rb=$(git rev-parse "$rb_dep")
			fi
		else
			echo "$rb_dep" >&2
		fi
	done
	printf '%s\n' "$rb"
}

# runnable_tests -- of the paths on stdin, the test scripts the runner would
# execute. tests/lib/ is sourced helpers, never tests, and a file without the
# executable bit is not a test either (tests/client/fs-filter-common.sh is mode
# 644 for exactly that reason).
runnable_tests() {
	while IFS= read -r f; do
		case $f in
			tests/lib/*) continue ;;
			tests/*.sh) ;;
			*) continue ;;
		esac
		[ -x "$f" ] && printf '%s\n' "$f"
	done
}

# ---------------------------------------------------------------------------
# evaluate LABEL MB REF RESTORE
#
# One unit of work: remove its fix, rebuild, run its tests, put it back. The
# same for a pull request on the integration branch and for the branch in front
# of you -- only where MB and REF come from differs. Appends "LABEL<TAB>VERDICT
# <TAB>TEST<TAB>NOTE" lines to $work/findings.
# ---------------------------------------------------------------------------
evaluate() {
	ev_label=$1 ev_mb=$2 ev_ref=$3 ev_restore=$4

	changed "$ev_mb" "$ev_ref" >"$work/files"
	runnable_tests <"$work/files" >"$work/tests"
	[ -s "$work/tests" ] || return 0

	# Only tests that pass on the intact tree can be interpreted at all.
	: >"$work/usable"
	while IFS= read -r t; do
		if grep -qxF "0 $t" "$work/qualified"; then printf '%s\n' "$t" >>"$work/usable"; fi
	done <"$work/tests"
	if [ ! -s "$work/usable" ]; then
		finding "$ev_label" "no usable test" "" \
			"none of its tests pass on the intact tree"
		return 0
	fi

	# The revert set: everything it touched, less the tests being run, less
	# anything it ADDS under tests/ (see the header).
	git diff --name-only --diff-filter=A "$ev_mb..$ev_ref" -- tests/ >"$work/added"
	cat "$work/usable" "$work/added" >"$work/keep"
	grep -vxF -f "$work/keep" "$work/files" >"$work/revert" 2>/dev/null || :
	[ -s "$work/revert" ] || { finding "$ev_label" "skipped" "" "nothing to revert"; return 0; }

	ev_live=0
	while IFS= read -r f; do
		is_inert "$f" || ev_live=1
	done <"$work/revert"
	if [ "$ev_live" = 0 ]; then
		finding "$ev_label" "skipped" "" "no runtime change"
		return 0
	fi

	# xargs rather than $(cat): a pull request may touch more paths than a
	# command line will hold.
	xargs git diff "$ev_mb..$ev_ref" -- <"$work/revert" >"$work/patch" 2>/dev/null
	if ! git apply -R "$work/patch" 2>"$work/apply.err"; then
		finding "$ev_label" "cannot verify" "" \
			"the revert would not apply -- something else rewrote the same lines"
		return 0
	fi

	# A failed rebuild is not a neutral event: make leaves the binaries built
	# from the code that was just taken out, every test then passes against the
	# fix it was supposed to be missing, and the whole change reads as
	# unguarded. Removing part of a change often does not compile -- a caller
	# left behind, a helper that no longer exists -- so this has to be checked
	# rather than assumed.
	if ! build_tree; then
		finding "$ev_label" "cannot verify" "" \
			"the tree does not build with this change removed, so its tests could only have run against the old binaries"
		git checkout -q -f "$ev_restore" -- . 2>/dev/null
		git clean -qfd tests 2>/dev/null
		build_tree
		return 0
	fi

	while IFS= read -r t; do
		run_test "$t"
		ev_rc=$?
		case $ev_rc in
			0)
				# A test the change adds was written for it; one it only
				# edits may have been adapted to it -- a new variable, a
				# moved path -- and passing without it says nothing.
				if is_control "$t"; then
					finding "$ev_label" "control" "$t" ""
				elif grep -qxF "$t" "$work/added"; then
					finding "$ev_label" "FLAG" "$t" ""
				else
					finding "$ev_label" "modified" "$t" ""
				fi ;;
			124) finding "$ev_label" "no usable test" "$t" "timed out without the fix" ;;
			*)   finding "$ev_label" "ok" "$t" "rc=$ev_rc" ;;
		esac
	done <"$work/usable"

	git checkout -q -f "$ev_restore" -- . 2>/dev/null
	git clean -qfd tests 2>/dev/null
	build_tree
}

finding() {
	printf '%s\t%s\t%s\t%s\n' "$1" "$2" "$3" "$4" >>"$work/findings"
}

# qualify -- run every test named in $work/alltests once, on the intact tree,
# recording "RC PATH".
qualify() {
	sort -u "$work/alltests" >"$work/alltests.u"
	: >"$work/qualified"
	while IFS= read -r t; do
		run_test "$t"
		printf '%s %s\n' "$?" "$t" >>"$work/qualified"
	done <"$work/alltests.u"
}

# ---------------------------------------------------------------------------
# report -- the findings, grouped, to stdout and to the job summary if there is
# one. Never exits non-zero on a finding: it is for a person to act on, and a
# red tick on an unrelated pull request is not how to deliver it.
# ---------------------------------------------------------------------------
report() {
	r_head=$1
	{
		echo "## Tests that no longer prove their fix"
		echo
		echo "$r_head"
		echo
		r_good=$(awk '$1 == 0' "$work/qualified" | wc -l | tr -d ' ')
		r_all=$(wc -l <"$work/qualified" | tr -d ' ')
		echo "$r_good of $r_all tests pass on that tree intact, and only those can be interpreted below."
		echo

		if awk -F'\t' '$2 == "FLAG"' "$work/findings" | grep -q .; then
			r_n=$(awk -F'\t' '$2 == "FLAG"' "$work/findings" | wc -l | tr -d ' ')
			echo "### Unguarded ($r_n)"
			echo
			echo "These passed with their own fix removed. The fix is in, and nothing would notice if it went out again."
			echo
			# FILENAME rather than the usual "NR == FNR": with an empty
			# titles file -- every mode but the integration sweep -- no
			# records come from it, so NR == FNR stays true for the whole
			# of the second file and every finding is swallowed as a title.
			awk -F'\t' -v tf="$work/titles" '
				FILENAME == tf { title[$1] = $2; next }
				$2 == "FLAG" {
					n = $1; sub(/^#/, "", n)
					printf "- **%s** `%s`\n", $1, $3
					if (title[n] != "") printf "  %s\n", title[n]
					print "  To fix: assert what only the fix produces, not what its absence destroys -- a guard elsewhere can stop the damage, and then the damage is no longer evidence."
					print "  If it is meant to pass either way, say so in its header: `# control: passes with and without the fix`"
					print ""
				}' "$work/titles" "$work/findings"
		else
			echo "Nothing unguarded."
			echo
		fi

		if awk -F'\t' '$2 == "modified"' "$work/findings" | grep -q .; then
			r_n=$(awk -F'\t' '$2 == "modified"' "$work/findings" | wc -l | tr -d ' ')
			echo "### Passing without the change, which only edited them ($r_n)"
			echo
			echo "These existing tests passed with the change removed. A change often edits a test only to keep it running -- a new variable, a moved path -- and then this says nothing; but if the edit was meant to pin the change, the fix is unguarded. Read the test's diff to tell which."
			echo
			awk -F'\t' '$2 == "modified" { printf "- **%s** `%s`\n", $1, $3 }' "$work/findings"
			echo
		fi

		for r_v in control "cannot verify" "no usable test" skipped; do
			case $r_v in
				control)          r_title="Declared controls" ;;
				"cannot verify")  r_title="Not verified" ;;
				"no usable test") r_title="No usable test" ;;
				skipped)          r_title="Skipped" ;;
			esac
			awk -F'\t' -v v="$r_v" '$2 == v' "$work/findings" >"$work/group"
			[ -s "$work/group" ] || continue
			r_n=$(wc -l <"$work/group" | tr -d ' ')
			echo "### $r_title ($r_n)"
			echo
			awk -F'\t' '{
				line = "- " $1
				if ($3 != "") line = line " " $3
				if ($4 != "") line = line " -- " $4
				print line
			}' "$work/group"
			echo
		done

		if [ -s "$work/narrow" ]; then
			echo "### Declared dependencies not in the history"
			echo
			echo "These declare a dependency their branch does not contain, so its commits are counted as theirs. Rebase onto the dependency to narrow the range."
			echo
			awk -F'\t' '{ print "- #" substr($1, 2) " -- " $2 }' "$work/narrow"
			echo
		fi

		r_ok=$(awk -F'\t' '$2 == "ok"' "$work/findings" | wc -l | tr -d ' ')
		if [ "$r_ok" = 1 ]; then
			echo "1 test/fix pair checked out sound."
		else
			echo "$r_ok test/fix pairs checked out sound."
		fi
	} >"$work/report.md"

	cat "$work/report.md"
	if [ -n "${GITHUB_STEP_SUMMARY:-}" ]; then
		cat "$work/report.md" >>"$GITHUB_STEP_SUMMARY"
	fi
}

# ---------------------------------------------------------------------------
# local mode
# ---------------------------------------------------------------------------
run_local() {
	dirty=$(git status --porcelain --untracked-files=no)
	[ -z "$dirty" ] || die "the working tree has uncommitted changes, and this reverts files in it. Commit or stash first:
$dirty"

	head_sha=$(git rev-parse HEAD) || die "not a git checkout"
	base_ref=$BASE
	git rev-parse -q --verify "$base_ref" >/dev/null 2>&1 || base_ref="up/$BASE"
	git rev-parse -q --verify "$base_ref" >/dev/null 2>&1 \
		|| die "cannot find $BASE to compare against"
	mb=$(git merge-base "$base_ref" HEAD) \
		|| die "cannot find where this branch forks from $BASE"
	branch=$(git rev-parse --abbrev-ref HEAD)

	# A stacked branch carries its dependencies' commits. Without --after, ask
	# GitHub which open pull request this commit heads and read what it
	# declares; without gh, or with no such pull request, the range stays at
	# the fork point and the report says so.
	if [ -z "$after" ] && command -v gh >/dev/null 2>&1; then
		l_pr=$(gh pr list --repo "$REPO" --state open --limit 300 \
			--json number,headRefOid \
			-q ".[] | select(.headRefOid == \"$head_sha\") | .number" 2>/dev/null | head -n 1)
		if [ -n "$l_pr" ]; then
			for d in $(gh pr view "$l_pr" --repo "$REPO" --json body -q .body 2>/dev/null \
				| declared_deps); do
				git fetch -q "https://github.com/$REPO.git" \
					"+refs/pull/$d/head:refs/pin-check/dep/$d" 2>/dev/null \
					&& after="$after refs/pin-check/dep/$d"
			done
		fi
	fi
	if [ -n "$after" ]; then
		# shellcheck disable=SC2086 # a list of refs, split on purpose
		mb=$(range_base "$mb" HEAD $after 2>"$work/missed")
		while IFS= read -r d; do
			printf '%s\tits history does not contain %s, so that dependency stays in the range\n' \
				"#$branch" "${d#refs/pin-check/dep/}" >>"$work/narrow"
		done <"$work/missed"
	fi

	build_tree || die "this tree does not build; nothing below would mean anything"

	changed "$mb" HEAD | runnable_tests >"$work/alltests"
	[ -s "$work/alltests" ] \
		|| die "this branch touches no test the runner would execute"
	qualify

	: >"$work/findings"
	: >"$work/titles"
	evaluate "$branch" "$mb" "$head_sha" "$head_sha"
	report "Branch \`$branch\`, against \`$BASE\`. This says the tests fail without the fix *today*; only the integration run can say whether something merged later leaves them passing."
}

# ---------------------------------------------------------------------------
# commit mode -- a change that has already landed
# ---------------------------------------------------------------------------
#
# For a merged pull request the range has to be given rather than derived:
# once it is an ancestor of main, "git merge-base main pr/N" collapses to the
# pull request itself and the derived range comes out empty. Name its merge
# commit and this works it out -- M^1..M^2 for a merge, sha^..sha otherwise.
#
# The ground defaults to that same commit, which is the tree as the change
# landed: fix and test from the same day, so nothing is being judged by a test
# written months later against a tree that never saw it. --against overrides
# it, for asking the question at some other point in history.
run_commit() {
	dirty=$(git status --porcelain --untracked-files=no)
	[ -z "$dirty" ] || die "the working tree has uncommitted changes, and this checks out another commit and reverts files in it. Commit or stash first:
$dirty"

	case $commit in
		*..*)
			c_a=${commit%%..*}; c_b=${commit##*..}
			c_ground=$c_b ;;
		*)
			if git rev-parse -q --verify "$commit^2" >/dev/null 2>&1; then
				# A merge commit. Its first parent is the base branch as it
				# stood at merge time, which is NOT the fork point: where the
				# branch was behind, "M^1..M^2" also carries every later base
				# change in reverse. #114 measures as 302 files that way and 15
				# from the fork point.
				c_b=$commit^2
				c_a=$(git merge-base "$commit^1" "$commit^2") \
					|| die "cannot find where $commit's branch forked"
			else
				c_a=$commit^;  c_b=$commit        # a squash, or a plain commit
			fi
			c_ground=$commit ;;
	esac
	[ -z "$against" ] || c_ground=$against

	for r in "$c_a" "$c_b" "$c_ground"; do
		git rev-parse -q --verify "$r" >/dev/null 2>&1 || die "no such commit: $r"
	done

	c_here=$(git symbolic-ref -q --short HEAD || git rev-parse HEAD)
	git checkout -q -f --detach "$c_ground" || die "cannot check out $c_ground"

	# No "git clean -fd" anywhere near this: in a configured tree Makefile and
	# include/config.h are untracked, and removing them unconfigures it -- every
	# test that needs a binary then skips, which reads as a fix correctly pinned.
	build_tree || die "$c_ground does not build; nothing below would mean anything"

	changed "$c_a" "$c_b" | runnable_tests >"$work/alltests"
	[ -s "$work/alltests" ] \
		|| die "that range touches no test the runner would execute"
	qualify

	: >"$work/findings"
	: >"$work/titles"
	c_label=${subject:-$(git rev-parse --short "$c_a")..$(git rev-parse --short "$c_b")}
	evaluate "$c_label" "$c_a" "$c_b" "$c_ground"
	report "$c_label (\`$(git rev-parse --short "$c_a")..$(git rev-parse --short "$c_b")\`), checked on \`$(git rev-parse --short "$c_ground")\` -- the tree it landed on."

	git checkout -q -f "$c_here" 2>/dev/null
}


# ---------------------------------------------------------------------------
# integration mode
# ---------------------------------------------------------------------------
fetch_heads() {
	git remote add up "https://github.com/$REPO.git" 2>/dev/null || :
	git fetch -q up "+refs/heads/$BASE:refs/remotes/up/$BASE" \
		"+refs/pull/*/head:refs/remotes/pr/*" \
		|| die "cannot fetch $REPO"
}

# Ready pull requests before drafts, each group in number order, so a draft
# cannot keep a reviewable one out of the integration branch.
list_prs() {
	gh pr list --repo "$REPO" --state open --limit 300 \
		--json number,title,isDraft \
		-q '.[] | [.number, (if .isDraft then "draft" else "ready" end), .title] | @tsv' \
		>"$work/prs.raw" || die "cannot list pull requests"
	awk -F'\t' '$2 == "ready" { print $1 }' "$work/prs.raw" | sort -n >"$work/prs"
	awk -F'\t' '$2 == "draft" { print $1 }' "$work/prs.raw" | sort -n >>"$work/prs"
	awk -F'\t' '{ print $1 "\t" $3 }' "$work/prs.raw" >"$work/titles"
}

build_integration() {
	git checkout -q -f -B _pincheck "up/$BASE" || die "cannot check out up/$BASE"
	: >"$work/merged"
	: >"$work/notmerged"
	while IFS= read -r n; do
		if ! git rev-parse -q --verify "pr/$n" >/dev/null 2>&1; then
			printf '%s\n' "$n" >>"$work/notmerged"; continue
		fi
		if git merge -q --no-edit "pr/$n" >/dev/null 2>&1; then
			printf '%s\n' "$n" >>"$work/merged"
		else
			git merge --abort >/dev/null 2>&1
			git reset -q --hard
			git clean -qfd tests 2>/dev/null
			printf '%s\n' "$n" >>"$work/notmerged"
		fi
	done <"$work/prs"
}

run_integration() {
	# Put back what was checked out, as --commit does: the integration branch
	# is a working state, not somewhere to leave a contributor's checkout --
	# nor the next run, which would start from the version of this script the
	# integration branch carries.
	i_here=$(git symbolic-ref -q --short HEAD || git rev-parse HEAD)
	fetch_heads
	list_prs
	build_integration
	integ=$(git rev-parse HEAD)
	base_sha=$(git rev-parse "up/$BASE")

	build_tree || die "the integration branch does not build; nothing below would mean anything"

	# The header reports what the branch is made of; --pr narrows only what
	# gets evaluated on it.
	cp "$work/merged" "$work/evaluate"
	if [ -n "$only" ]; then
		grep -qx "$only" "$work/merged" \
			|| die "#$only is not on the integration branch: it is not open, or it does not merge onto $BASE"
		printf '%s\n' "$only" >"$work/evaluate"
	fi

	: >"$work/alltests"
	: >"$work/narrow"
	while IFS= read -r n; do
		mb=$(git merge-base "$base_sha" "pr/$n" 2>/dev/null) || mb=$base_sha
		deps=$(gh pr view "$n" --repo "$REPO" --json body -q .body 2>/dev/null \
			| declared_deps)
		set --
		for d in $deps; do
			git rev-parse -q --verify "pr/$d" >/dev/null 2>&1 && set -- "$@" "pr/$d"
		done
		if [ $# -gt 0 ]; then
			mb=$(range_base "$mb" "pr/$n" "$@" 2>"$work/missed")
			while IFS= read -r d; do
				printf '#%s\tits history does not contain %s, so that dependency stays in the range\n' \
					"$n" "${d#pr/}" >>"$work/narrow"
			done <"$work/missed"
		fi
		printf '%s %s\n' "$n" "$mb" >>"$work/mb"
		changed "$mb" "pr/$n" | runnable_tests >>"$work/alltests"
	done <"$work/evaluate"
	qualify

	: >"$work/findings"
	while IFS= read -r n; do
		mb=$(awk -v n="$n" '$1 == n {print $2; exit}' "$work/mb")
		evaluate "#$n" "$mb" "pr/$n" "$integ"
	done <"$work/evaluate"

	n_merged=$(wc -l <"$work/merged" | tr -d ' ')
	n_not=$(wc -l <"$work/notmerged" | tr -d ' ')
	if [ -n "$only" ]; then
		report "Integration branch: \`$BASE\` + $n_merged open pull requests ($n_not could not be merged onto it). Evaluating #$only only."
	else
		report "Integration branch: \`$BASE\` + $n_merged open pull requests ($n_not could not be merged onto it)."
	fi

	git checkout -q -f "$i_here" 2>/dev/null
	git branch -q -D _pincheck 2>/dev/null
}

# --pr does not ask you to know whether it is still open. An open one is checked
# on the integration branch; a merged one on the tree it landed on, which needs
# its merge commit -- so look that up rather than making the caller paste it.
if [ "$mode" = one ]; then
	pr_state=$(gh pr view "$only" --repo "$REPO" --json state,mergeCommit \
		-q '[.state, (.mergeCommit.oid // "")] | @tsv' 2>/dev/null) \
		|| die "cannot read pull request #$only from $REPO"
	case $pr_state in
		OPEN*)
			: ;;
		MERGED*)
			commit=${pr_state##*	}
			subject="#$only, merged"
			[ -n "$commit" ] \
				|| die "#$only is merged but has no merge commit recorded, so its range cannot be worked out. Name it yourself: --commit A..B"
			mode=commit ;;
		CLOSED*)
			die "#$only was closed without merging, so its fix was never applied and there is nothing to remove. Nothing to check." ;;
		*)
			die "cannot tell the state of #$only" ;;
	esac
fi

case $mode in
	declared)  declared_deps; exit 0 ;;
	rangebase) range_base "$@"; exit 0 ;;
	local)  run_local ;;
	commit) run_commit ;;
	*)      run_integration ;;
esac
