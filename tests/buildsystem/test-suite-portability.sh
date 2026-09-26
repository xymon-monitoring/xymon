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
# A `case` inside $(...) trips the bash 3.2 command-substitution parser -- the
# very shell this suite pins as the floor (macOS ships 3.2), where the `)` of a
# case pattern is mistaken for the end of the substitution. Keep the filter in a
# function the substitution only calls, so no case is parsed inside $(...).
_emit_shell_scripts() {
	local f
	while IFS= read -r f; do
		case $f in
			*.sh) printf '%s\n' "$f" ;;
			*) head -n 1 "$f" 2>/dev/null | grep -q '^#!.*sh' && printf '%s\n' "$f" ;;
		esac
	done
}
files=$(
	find "$ROOT/tests" -type f \( -name '*.sh' -o -perm -100 \) ! -name "$(basename "$0")" |
	_emit_shell_scripts | sort -u
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

# An -I directory is searched for <angle> includes too, so a source directory
# put there can capture a system header. macOS makes that concrete: the SDK's
# stdio.h includes <Availability.h>, the filesystem is case-insensitive, and
# lib/availability.h answers instead -- every system type after it is unknown,
# and the three tests that reached lib/ this way did not build on the macOS
# lanes. -iquote is searched for "quoted" includes only, which is all an
# in-tree header needs. Generated directories are deliberately NOT flagged:
# rrd-api-compat.sh puts its mock <rrd.h> on -I precisely so that an angle
# include finds it.
report "include-shadow" '\-I *"?\$\{?ROOT' \
	"an -I source directory also answers <angle> includes; use -iquote (macOS: lib/availability.h captures the SDK's Availability.h)"

# A test replaces an OS primitive by naming the helper that holds it, not by
# rewriting the path it reads. A rewritten path only works for the one OS that
# has that path, so the same test cannot cover the other four clients -- and
# the client is then free to reach for the primitive anywhere, which is how
# /proc ended up in three places in the Linux [df] block.
# Any sed reaching for /proc, not one delimiter's worth: s;...;...; and s|...|
# rewrite a path just as well as s#...#, and a rule that knows four spellings
# is a rule the next test writer works around by accident. A test that has to
# name the path anyway -- the native ones -- reads it, it does not sed it.
report "path-rewrite" 'sed.*/proc/' \
	"replace the helper by name (fsf_stub_helper), not the path it reads"

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

# A uname guard is how a test stops covering four of the five clients. It is
# allowed in exactly one place -- the test of an OS primitive itself, which
# cannot run anywhere else -- and such a test says so in its header:
#
#     # native-primitive: fs_procname
#
# The declaration is the exemption, not the filename, so a test that grows a
# uname guard has to state which primitive it is there to exercise.
# check_uname_guard FILE OUT -- append this file's uname violations to OUT.
check_uname_guard() {
	local f=$1 out=$2 code
	code=$(grep -vE '^[[:space:]]*#' "$f" || true)
	printf '%s\n' "$code" | grep -q 'uname' || return 0
	grep -qE '^#[[:space:]]*native-primitive:[[:space:]]*[A-Za-z_]' "$f" && return 0
	printf 'uname-guard\t%s\t%s\n' "${f#"$ROOT"/}" \
		"guards on uname without a '# native-primitive: NAME' header; stub the helper instead so the test covers every client" >>"$out"
}

for f in $files; do check_uname_guard "$f" "$work/violations"; done

# "\?", "\+" and "\|" are GNU (and Spencer) extensions to a *basic* regex, not
# POSIX. OpenBSD's grep reads them as the literal characters, so a pattern
# using one silently stops matching there -- and a test that greps for
# something it produced itself then reports the thing it was checking as
# changed, which is the wrong place to look. In an ERE all three are standard,
# so the fix is -E (and escaping the pipes that were literal before).
# check_posix_bre FILE OUT -- append this file's BRE violations to OUT.
check_posix_bre() {
	local f=$1 out=$2 line n
	{ grep -nE '(^|[^[:alnum:]_])(grep|sed)([^[:alnum:]_]|$)' "$f" 2>/dev/null || true; } | \
	{ grep -vE '^[0-9]+:[[:space:]]*#' || true; } | \
	while IFS= read -r line; do
		if printf '%s' "$line" | grep -qE '(grep|sed)[[:space:]]+-[[:alnum:]]*[EP]'; then
			continue	# -E (or -P): the operators below are the standard ones
		fi
		if printf '%s' "$line" | grep -qE '\\[?+|]'; then
			n=${line%%:*}
			printf 'posix-bre\t%s\t%s\n' "${f#"$ROOT"/}:$n" \
				'\? \+ and \| are not POSIX in a basic regex; OpenBSD reads them literally. Use -E' >>"$out"
		fi
	done
}

for f in $files; do check_posix_bre "$f" "$work/violations"; done

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
	printf 'cc -I"$ROOT/lib" x.c\n'
	printf 'sed s#/proc/mounts#f# x\n'
} >"$probe"
probe_pattern=$(IFS='|'; printf '%s' "${__rule_patterns[*]}")
hits=$(grep -cE "$probe_pattern" "$probe")
assert_equal "8" "$hits" "the rule patterns no longer match the constructs they are meant to catch"

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

# The BRE rule the same way, and with the ERE beside it: a rule that fired on
# both would be met by turning every -E off again.
brefile="$work/bre.sh"
{
	printf '#!/usr/bin/env bash\n'
	printf "grep -q 'a\\\\|b' f\n"
	printf "grep -Eq 'a\\|b' f\n"
	printf "sed -n 's/x\\\\+/y/p' f\n"
} >"$brefile"
: >"$work/bre-violations"
check_posix_bre "$brefile" "$work/bre-violations"
assert_equal "2" "$(wc -l <"$work/bre-violations" | tr -d " ")" \
	"the POSIX-BRE rule no longer separates a GNU-only basic regex from an extended one"

# The uname rule the same way, on both sides: a guard without the declaration
# is reported, the same guard with it is not.
: >"$work/uname-violations"
undeclared="$work/undeclared.sh"
{
	printf '#!/usr/bin/env bash\n'
	printf '[ "$(uname -s)" = Linux ] || exit 77\n'
} >"$undeclared"
check_uname_guard "$undeclared" "$work/uname-violations"
assert_equal "1" "$(wc -l <"$work/uname-violations" | tr -d " ")" \
	"the uname rule no longer reports a test that guards on uname without declaring its primitive"

declared="$work/declared.sh"
{
	printf '#!/usr/bin/env bash\n'
	printf '# native-primitive: fs_procname\n'
	printf '[ "$(uname -s)" = Linux ] || exit 77\n'
} >"$declared"
: >"$work/uname-violations"
check_uname_guard "$declared" "$work/uname-violations"
assert_equal "0" "$(wc -l <"$work/uname-violations" | tr -d " ")" \
	"the uname rule now reports a declared native-primitive test, which is the one place a guard belongs"

pass "the test suite keeps to bash 3.2, portable tool flags, the configured build flags, and one lane per client"
