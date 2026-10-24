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
			*) grep -q '^#!.*sh' <<<"$(head -n 1 "$f" 2>/dev/null)" && printf '%s\n' "$f" ;;
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
		# The runner is scanned like everything else -- it has to run on every
		# platform too -- except by the rules that are about what a TEST may
		# use: it is the file that defines those, and reporting it would mean
		# the rule could only be satisfied by deleting the definition.
		case $rule in
		  runner-only-helper) [ "$f" = "$ROOT/tests/testsuite" ] && continue ;;
		esac
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
# head -c is not POSIX either: OpenBSD's head has none.
report "gnu-only" 'sed +-i|grep +[^|]*--include|grep +-[a-zA-Z]*P|stat +-c|readlink +-f|date +-d |head +-c' \
	"GNU-only or differently-spelled on BSD/macOS; use a portable form"

# GNU-only regex escapes. The BSD greps take \b as a literal b, so a rule
# written with it silently matches nothing there -- which is how this very
# file shipped a mapfile rule that tested nothing on OpenBSD. Reported whatever
# follows the escape: \b is usually written right before a word.
gnu_regex_pattern='(grep|sed|awk|expr)[^|]*\\(b|<|>|w|W|s|S|d|D)'
report "gnu-regex" "$gnu_regex_pattern" \
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
# The rrdtool command-line tool is a separate package from the library the
# tree links against, and is not installed on every lane. A test that gates on
# it does not fail there - it quietly checks less, which is worse: the half it
# skips is usually the value, and the half it keeps is usually the filename.
# Read a RRD through librrd instead; tests/rrd/inode-unix-columns-harness.c is
# the shape to copy.
report "rrdtool-cli" '(command -v|which) +rrdtool|(^|[^-[:alnum:]_])rrdtool +(lastupdate|fetch|info|create|update|graph)' \
	"the rrdtool CLI is not installed everywhere; read the RRD through librrd"

# The runner defines errline() and run_one() for itself. A test does not have
# them: it is sourced with tests/lib/assert.sh and nothing else, so a call to
# one is a command-not-found. Such a call lands on the arm that REPORTS a
# failure, which is the arm that does not run while everything passes -- so it
# hides in a green suite and the assertion it belongs to can never fire.
# Measured: tests/buildsystem/testsuite-run-disposal.sh asserted a signal exit
# status through errline() and could not have reported a wrong one.
# The message stays on one line: violations are stored one per line.
report "runner-only-helper" '(^|[^-[:alnum:]_.])(errline|run_one)[[:space:]]' \
	"errline and run_one belong to tests/testsuite, not to a test: a call is a command-not-found on the branch that reports failures; use fail, skip or pass from tests/lib/assert.sh"

# A cleanup that kills returns while the process is still exiting, and the
# temp directory removed next is written into meanwhile: xymond writes its
# checkpoint as it exits. rm -rf then fails on the new file, the directory is
# left behind and tests/final/no-leftovers.sh fails, on slow lanes only (macOS
# 26 and OpenBSD 7.9, #586 and #622). stop_process waits for the exit. pkill
# is not refused: it is how a cleanup reaches a stopped process's children.
report "cleanup-kill" 'register_cleanup .*(^|[^[:alnum:]_-])kill([[:space:]]|$)' \
	"a cleanup that kills returns before the process has exited, and the temp dir can be written into as it is removed; use stop_process from tests/lib/assert.sh"

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
	# A here-string, not printf | grep -q: grep -q exits at its first match,
	# and a printf still writing then dies of SIGPIPE, which pipefail turns
	# into a reported violation -- 901 times in 20000 on OpenBSD 7.9.
	# [{]? rather than \{? : a brace after a backslash is not portable ERE.
	grep -qE '[$][{]?ROOT[}]?[^[:space:]]*[.]a' <<<"$code" || return 0
	grep -q 'xymon_cflags' <<<"$code" \
		|| printf 'link-flags\t%s\t%s\n' "${f#"$ROOT"/}" "links an in-tree archive without xymon_cflags" >>"$out"
	grep -q 'xymon_ldflags' <<<"$code" \
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
	grep -q 'uname' <<<"$code" || return 0
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
		if grep -qE '(grep|sed)[[:space:]]+-[[:alnum:]]*[EP]' <<<"$line"; then
			continue	# -E (or -P): the operators below are the standard ones
		fi
		if grep -qE '\\[?+|]' <<<"$line"; then
			n=${line%%:*}
			printf 'posix-bre\t%s\t%s\n' "${f#"$ROOT"/}:$n" \
				'\? \+ and \| are not POSIX in a basic regex; OpenBSD reads them literally. Use -E' >>"$out"
		fi
	done
}

for f in $files; do check_posix_bre "$f" "$work/violations"; done

# A pipe into a reader that stops early -- grep -q or -m, head -- races
# under pipefail: once the reader exits, a producer still writing dies of
# SIGPIPE (141) and the command fails although the reader got its answer. It
# failed 901 times in 20000 on OpenBSD 7.9 (#594); a slow scheduler is what
# loses the race, so a fast one never shows it. Feed grep a here-string and
# take a first line with first_line() instead. awk ... exit, sed ...q and cmp
# stop early too: channel-hup-keeps-worker.sh piped ps into an awk that exits
# at its first match, and failed on the macOS lanes with "ps: stdout: Broken
# pipe" whenever enough of ps's output followed the match. An exit in an END
# block is not early -- awk has read everything by then. A pipe continued
# onto the next line, after a backslash or a trailing |, is joined first, and
# reported at the line it starts on.
# check_early_exit_pipe FILE OUT -- append this file's violations to OUT.
check_early_exit_pipe() {
	local f=$1 out=$2 n
	# The whole file is read first, so a line can be joined to the next.
	# [ \t], not [[:space:]]: older mawks have no character classes.
	awk '
	function early(s,    rest, prog, i, q) {
		q = "\047"
		while (match(s, /(^|[^|])[|][ \t]*/)) {
			rest = substr(s, RSTART + RLENGTH)
			s = rest
			# The second | of a ||: what follows does not read a pipe.
			if (rest ~ /^[|]/) { s = substr(rest, 2); continue }
			if (rest ~ /^(grep|egrep|fgrep)[ \t]+(-[A-Za-z0-9]+[ \t]+)*-[A-Za-z0-9]*[qm]/) return 1
			if (rest ~ /^head([ \t]|$|[)])/) return 1
			if (rest ~ /^cmp([ \t]|$)/) return 1
			if (rest ~ /^sed[ \t]/) {
				sub(/[|].*/, "", rest)
				if (rest ~ ("[;{ \t0-9/$" q "]q[ \t]*($|[;}" q "])")) return 1
			}
			if (rest ~ /^awk[ \t]/) {
				# The program is the first quoted word standing on its own; a
				# quote right after -F belongs to the separator.
				prog = ""
				for (i = 4; i <= length(rest); i++)
					if (substr(rest, i, 1) == q && substr(rest, i - 1, 1) ~ /[ \t]/) {
						prog = substr(rest, i + 1)
						sub(q ".*", "", prog)
						break
					}
				gsub(/END[ \t]*[{][^}]*[}]/, "", prog)
				if (prog ~ /(^|[^A-Za-z0-9_])exit([^A-Za-z0-9_]|$)/) return 1
			}
		}
		return 0
	}
	{ line[NR] = $0 }
	END {
		for (i = 1; i <= NR; i++) {
			if (line[i] ~ /^[ \t]*#/) continue
			start = i
			buf = line[i]
			while (i < NR) {
				if (buf ~ /\\$/) sub(/\\$/, "", buf)
				else if (buf !~ /(^|[^|])[|][ \t]*$/ && line[i + 1] !~ /^[ \t]*[|]([^|]|$)/) break
				i++
				buf = buf " " line[i]
			}
			if (early(buf)) print start
		}
	}
	' "$f" |
	while IFS= read -r n; do
		printf 'early-exit-pipe\t%s\t%s\n' "${f#"$ROOT"/}:$n" \
			'pipes into a reader that stops early (grep -q/-m, head, awk ... exit, sed ...q, cmp); under pipefail the producer can die of SIGPIPE. Capture the output first and read it from a here-string, or use first_line' >>"$out"
	done
}

for f in $files; do check_early_exit_pipe "$f" "$work/violations"; done

# git, the compiler and make speak the user's language: a German git writes
# "KONFLIKT" where an English one writes "CONFLICT", and pin-check.sh failed
# on every German system for asserting the English word (#628). Xymon's own
# messages are not translated, so only these programs' words are refused, and
# only where a test asserts on them -- unless the same line runs with LC_ALL=C.
# check_translated FILE OUT -- append this file's violations to OUT.
translated_pattern='(assert_(not_)?contains|grep)[^#]*(CONFLICT|Automatic merge|fatal: |error: |Already up to date|nothing to commit|warning: |undefined reference|No rule to make)'
check_translated() {
	local f=$1 out=$2 line n
	{ grep -nE "$translated_pattern" "$f" 2>/dev/null || true; } | \
	{ grep -vE '^[0-9]+:[[:space:]]*#' || true; } | \
	while IFS= read -r line; do
		case $line in *LC_ALL=C*) continue ;; esac
		n=${line%%:*}
		printf 'translated\t%s\t%s\n' "${f#"$ROOT"/}:$n" \
			"asserts on a word git, the compiler or make translate; assert on what no translation changes, or run that command with LC_ALL=C (#628)" >>"$out"
	done
}

for f in $files; do check_translated "$f" "$work/violations"; done

# The same race in a cleanup function, registered by name: one that kills and
# neither waits (wait, a kill -0 loop) nor calls stop_process. The functions
# that wait for themselves are left alone; stop_process is what they do.
# check_cleanup_fn FILE OUT -- append this file's violations to OUT.
check_cleanup_fn() {
	local f=$1 out=$2 fn body
	for fn in $(sed -nE 's/^[[:space:]]*register_cleanup "?([A-Za-z_][A-Za-z0-9_]*)"?[[:space:]]*$/\1/p' "$f"); do
		body=$(awk -v fn="$fn" '$0 ~ "^[[:space:]]*" fn "\\(\\)" { p = 1 } p { print } p && /^[[:space:]]*}/ { exit }' "$f" |
			grep -vE '^[[:space:]]*#' || true)
		grep -qE '(^|[^[:alnum:]_-])kill([[:space:]]|$)' <<<"$body" || continue
		grep -qE 'stop_process|(^|[^[:alnum:]_])wait([[:space:]]|$)|kill +-0' <<<"$body" && continue
		printf 'cleanup-kill\t%s\t%s\n' "${f#"$ROOT"/}" \
			"cleanup $fn() kills without waiting; the temp dir can be written into as it is removed. Use stop_process from tests/lib/assert.sh" >>"$out"
	done
}

for f in $files; do check_cleanup_fn "$f" "$work/violations"; done

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
	printf 'head -c 2 f\n'
	printf 'python3 -c pass\n'
	printf 'grep -E %s x\n' "'\\bword\\b'"
	printf 'chmod 500 d\n'
	printf 'chmod 555 d\n'
	printf 'cc -I"$ROOT/lib" x.c\n'
	printf 'sed s#/proc/mounts#f# x\n'
	printf 'errline "boom"\n'
	printf 'register_cleanup "kill $pid 2>/dev/null || true"\n'
	printf 'rrdtool lastupdate x.rrd\n'
} >"$probe"
probe_pattern=$(IFS='|'; printf '%s' "${__rule_patterns[*]}")
hits=$(grep -cE "$probe_pattern" "$probe")
assert_equal "12" "$hits" "the rule patterns no longer match the constructs they are meant to catch"

# The cleanup-function rule the same way: a function that kills and does not
# wait is reported, one that waits or uses stop_process is not.
cleanfile="$work/cleanup.sh"
{
	printf '#!/usr/bin/env bash\n'
	printf 'stop_bare() {\n\tkill "$pid" 2>/dev/null || true\n}\nregister_cleanup stop_bare\n'
	printf 'stop_waits() {\n\tkill "$pid" 2>/dev/null || true\n\twait "$pid" 2>/dev/null || true\n}\nregister_cleanup stop_waits\n'
	printf 'stop_new() {\n\tstop_process "$pid"\n}\nregister_cleanup "stop_new"\n'
} >"$cleanfile"
: >"$work/cleanup-violations"
check_cleanup_fn "$cleanfile" "$work/cleanup-violations"
assert_equal "1" "$(wc -l <"$work/cleanup-violations" | tr -d " ")" \
	"the cleanup-function rule should report the one function that kills without waiting, and only it"
assert_contains "stop_bare()" "$(cat "$work/cleanup-violations")" \
	"the cleanup-function rule reported the wrong function"

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

# The GNU-escape rule the same way: an escape is reported whatever follows it,
# a word included, and the portable spellings are not.
gnufile="$work/gnu-regex.sh"
cat >"$gnufile" <<'EOF'
grep -qE '\bbreak;' <<<"$x"
grep -E '\bword' x
grep -E 'a\sb' f
grep -E '(^|[^[:alnum:]_])word' f
grep -E 'a[[:space:]]b' f
EOF
assert_equal "3" "$(grep -cE "$gnu_regex_pattern" "$gnufile")" \
	"the GNU-escape rule no longer reports an escape followed by a word, or reports a portable spelling"

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

# The early-exit rule the same way: each reader is reported after a pipe,
# continued lines included, and neither "||", an awk that exits only in END,
# a sed with no q command, nor the here-string form that replaces them is.
earlyfile="$work/early.sh"
{
	printf '#!/usr/bin/env bash\n'
	printf 'printf x | grep -q x\n'
	printf 'printf x | grep -Eqx x\n'
	printf 'printf x | grep -m1 x\n'
	printf 'v=$(printf x | head -1)\n'
	printf 'printf x | head -n 1\n'
	printf "ps | awk -v p=1 '\$2 == p { print \$1; exit }'\n"
	printf "ps | awk -F'|' '{ print; exit }'\n"
	printf "v=\$(ps 2>/dev/null \\\\\n\t| awk '/x/ { print \$2; exit }')\n"
	printf "ps |\n\tawk '{ exit }'\n"
	printf "ps | sed 5q\n"
	printf "ps | sed -n '/x/{p;q;}'\n"
	printf 'printf x | cmp -s - f\n'
	printf 'true || head -1 f\n'
	printf 'true || awk "{ exit }" f\n'
	printf "ps | awk '{ n++ } END { exit !n }'\n"
	printf "ps | awk -F'|' '{ print \$2 }'\n"
	printf "ps | sed -n 's/q/x/p'\n"
	printf 'grep -q x <<<"$v"\n'
	printf "awk '{ exit }' <<<\"\$v\"\n"
} >"$earlyfile"
: >"$work/early-violations"
check_early_exit_pipe "$earlyfile" "$work/early-violations"
assert_equal "2 3 4 5 6 7 8 9 11 13 14 15" \
	"$(cut -f2 "$work/early-violations" | sed 's/.*://' | tr '\n' ' ' | sed 's/ $//')" \
	"the early-exit rule no longer reports exactly the pipes into grep -q/-m, head, awk ... exit, sed ...q and cmp, at the line each starts on (and not ||, an exit in END, a sed with no q, or a here-string)"


# The translation rule the same way: an assertion on git's or the compiler's
# word is reported, the same word under LC_ALL=C is not, and neither is a
# word Xymon writes itself.
langfile="$work/lang.sh"
{
	printf '#!/usr/bin/env bash\n'
	printf 'assert_contains "CONFLICT" "$out" "git refused"\n'
	printf 'grep -q "warning: " cc.log\n'
	printf 'out=$(LC_ALL=C git merge x); assert_contains "CONFLICT" "$out" "git refused"\n'
	printf 'assert_contains "Address already in use" "$log" "xymond said"\n'
} >"$langfile"
: >"$work/lang-violations"
check_translated "$langfile" "$work/lang-violations"
assert_equal "2" "$(wc -l <"$work/lang-violations" | tr -d " ")" \
	"the translation rule no longer reports exactly the two assertions on translated words (and not one under LC_ALL=C, or one on Xymon's own)"

pass "the test suite keeps to bash 3.2, portable tool flags, the configured build flags, and one lane per client"
