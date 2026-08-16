#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/buildsystem/test-suite-portability.sh
#
# A test over the tests. Every rule here is a failure the suite actually had,
# each found by a BSD or macOS runner after passing on Linux -- which is the
# expensive way to learn it. They are cheap to check statically, so they are
# checked here, on every platform, before the slow lanes get a chance.
#
# Comment lines are skipped: several tests explain in a comment why they avoid
# a construct, and flagging that would punish the documentation.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
[ -d "$ROOT/tests" ] || skip "no tests/ directory in this checkout"

work=$(mktempdir)
: >"$work/violations"

# This file names the forbidden constructs, so it would flag itself. Its own
# non-vacuity probe below is what keeps the rules honest instead.
# Every shell script under tests/, not just the *.sh ones: the runner itself
# carries no extension, and it is the file that has to run on every platform.
files=$(
	find "$ROOT/tests" -type f \( -name '*.sh' -o -perm -100 \) ! -name "$(basename "$0")" |
	while IFS= read -r f; do
		case $f in
			*.sh) printf '%s\n' "$f" ;;
			*) head -n 1 "$f" 2>/dev/null | grep -q '^#!.*sh' && printf '%s\n' "$f" ;;
		esac
	done | sort -u
)
[ -n "$files" ] || fail "found no test scripts to check"

# report RULE PATTERN WHY -- flag every non-comment line matching PATTERN.
#
# Each pattern is kept, and the non-vacuity probe at the end matches with the
# ones recorded here rather than a copy of them. The copy is how the root-lane
# rule was widened while its probe still exercised the narrow form, so the
# probe kept passing over a gap it was there to expose.
__rule_patterns=()
report() {
	local rule=$1 pattern=$2 why=$3 f line

	__rule_patterns[${#__rule_patterns[@]}]=$pattern
	for f in $files; do
		# Drop comment lines before matching, keeping real line numbers.
		# "|| true" twice: a grep with no match exits 1, which set -e and
		# pipefail would take for an error.
		{ grep -nE "$pattern" "$f" 2>/dev/null || true; } | \
		{ grep -vE '^[0-9]+:[[:space:]]*#' || true; } | \
		while IFS= read -r line; do
			printf '%s\t%s:%s\t%s\n' "$rule" "${f#"$ROOT"/}" "${line%%:*}" "$why" >>"$work/violations"
		done
	done
}

# bash 3.2 is the floor: macOS ships it, and the suite runs under
# /usr/bin/env bash. mapfile cost us a whole macOS lane once (#339).
report "bash4" '(^|[^[:alnum:]_])(mapfile|readarray)([^[:alnum:]_]|$)|declare +-A|\$\{[A-Za-z_]+\^\^|\$\{[A-Za-z_]+,,' \
	"bash 4 only; macOS ships bash 3.2"

# Tool flags that exist on GNU and not on the BSDs, or mean different things.
report "gnu-only" 'sed +-i|grep +[^|]*--include|grep +-[a-zA-Z]*P|stat +-c|readlink +-f|date +-d ' \
	"GNU-only or differently-spelled on BSD/macOS; use a portable form"

# GNU-only regex escapes. The BSD greps take \b as a literal b, so a rule
# written with it silently matches nothing there -- which is how this very
# file shipped a mapfile rule that tested nothing on OpenBSD.
report "gnu-regex" '(grep|sed|awk|expr)[^|]*\\(b|<|>|w|W|s|S|d|D)([^[:alnum:]]|$)' \
	"GNU-only regex escape; POSIX has [[:alnum:]] and (^|[^...])"

# The lanes run as root, where a permission bit stops nothing. A test that
# takes write access away to force a failure therefore passes on a developer's
# machine and fails in CI, having proved the opposite of what it claims. Force
# the failure structurally instead - an existing directory where a file is
# expected, a path that is not there at all.
#
# The owner digit carries it: 6 and 7 are the two that grant both read and
# write, so [0-5] is exactly the modes that deny one of them, whatever the
# group and other digits say. Matching only the modes ending in 00 missed
# "chmod 555 dir", which is how a directory is usually made read-only
# (@SoundGoof).
report "root-lane" 'chmod +[-+=rwx]*[0-7]?[0-5][0-7][0-7]([^0-9]|$)|chmod +[ugoa]*-w' \
	"the lanes run as root; a permission bit will not force this failure"

# The rrdtool command-line tool is a separate package from the library the
# tree links against, and is not installed on every lane. A test that gates on
# it does not fail there - it quietly checks less, which is worse: the half it
# skips is usually the value, and the half it keeps is usually the filename.
# Read a RRD through librrd instead; tests/rrd/inode-unix-columns-harness.c is
# the shape to copy.
report "rrdtool-cli" '(command -v|which) +rrdtool|(^|[^-[:alnum:]_])rrdtool +(lastupdate|fetch|info|create|update|graph)' \
	"the rrdtool CLI is not installed everywhere; read the RRD through librrd"

# Interpreters the BSD runners do not have.
report "interpreter" '(^|[^-[:alnum:]_])(python3?|perl)[[:space:]]' \
	"not installed on the BSD lanes; awk and sh are"

# A harness that links the tree needs the configured flags: the BSDs keep
# pcre2.h under /usr/local or /usr/pkg, and without the rpath the binary links
# and then cannot find libpcre2 at run time.
# check_link_flags FILE OUT -- append this file's link-flags violations to OUT.
# Comments are stripped on both sides: a file that only *mentions* the helpers
# in a comment satisfied the rule, and one that names an archive in a comment
# was checked for no reason. Any in-tree archive counts, not one library-name
# family -- the flags are needed for whatever the tree builds.
check_link_flags() {
	local f=$1 out=$2 code
	code=$(grep -vE '^[[:space:]]*#' "$f" || true)
	# [{]? rather than \{? : a brace after a backslash is not portable ERE.
	printf '%s\n' "$code" | grep -qE '[$][{]?ROOT[}]?[^[:space:]]*[.]a' || return 0
	printf '%s\n' "$code" | grep -q 'xymon_cflags' \
		|| printf 'link-flags\t%s\t%s\n' "${f#"$ROOT"/}" "links an in-tree archive without xymon_cflags" >>"$out"
	printf '%s\n' "$code" | grep -q 'xymon_ldflags' \
		|| printf 'link-flags\t%s\t%s\n' "${f#"$ROOT"/}" "links an in-tree archive without xymon_ldflags" >>"$out"
}

for f in $files; do check_link_flags "$f" "$work/violations"; done

if [ -s "$work/violations" ]; then
	printf 'portability rules broken by the test suite:\n\n' >&2
	sort "$work/violations" | awk -F'\t' '{ printf "  [%s] %s\n      %s\n", $1, $2, $3 }' >&2
	fail "$(wc -l <"$work/violations" | tr -d ' ') violation(s); see above"
fi

# Not vacuous: the rules must fire on a file that breaks them.
probe="$work/probe.sh"
{
	printf '#!/usr/bin/env bash\n'
	printf 'mapfile -t x < /dev/null\n'
	printf 'sed -i s/a/b/ f\n'
	printf 'python3 -c pass\n'
	printf 'grep -E %s x\n' "'\\bword\\b'"
	printf 'chmod 500 d\n'
	printf 'chmod 555 d\n'
	printf 'rrdtool lastupdate x.rrd\n'
} >"$probe"
probe_pattern=$(IFS='|'; printf '%s' "${__rule_patterns[*]}")
hits=$(grep -cE "$probe_pattern" "$probe")
assert_equal "7" "$hits" "the rule patterns no longer match the constructs they are meant to catch"

# The link-flags rule the same way, including the bypass it used to allow: the
# helpers named in a comment and nowhere else.
bypass="$work/bypass.sh"
{
	printf '#!/usr/bin/env bash\n'
	printf '# built with xymon_cflags and xymon_ldflags, honest\n'
	printf '"$CC" -o x y.c "$ROOT/lib/libportable.a"\n'
} >"$bypass"
: >"$work/probe-violations"
check_link_flags "$bypass" "$work/probe-violations"
assert_equal "2" "$(wc -l <"$work/probe-violations" | tr -d " ")" \
	"the link-flags rule no longer reports both missing helpers for an archive built without them"

pass "the test suite keeps to bash 3.2, portable tool flags, and the configured build flags"
