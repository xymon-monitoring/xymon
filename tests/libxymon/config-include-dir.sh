#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/libxymon/config-include-dir.sh
#
# Regression guard for the ".d drop-in directory" mechanism shipped for every
# server and client config (#222): each shipped config ends with
#
#     optional directory @XYMONHOME@/etc/<name>.d
#
# so packagers and local admins can drop fragments into alerts.d, analysis.d,
# graphs.d, ... without editing the shipped file. 'optional' means a missing
# directory is silently ignored; 'directory' reads every file in it.
#
# Two things could regress independently, so the test pins both:
#
#   (A) Shipped artefact: every .DIST that PR #222 gave a drop-in directory
#       still declares it. A dropped or mistyped line silently disables the
#       drop-in for that config, which no build would catch.
#
#   (B) Mechanism: the 'optional directory' directive actually merges the
#       fragments AND tolerates the directory being absent. This is what the
#       shipped line relies on. Driven through the real stackio reader
#       (lib/stackio.c) built STANDALONE -- xymonstackio where there is one,
#       so an installed package's own binary is checked -- with a present dir
#       carrying two fragments and, for contrast, a missing dir with and
#       without 'optional'.
#       A drop-in that pulls in a file or a directory of its own is read to
#       its end before the next drop-in starts (#881).
#
#   (C) A queued drop-in that cannot be opened is skipped, and the next one
#       read in its place, whether it is the first or a later one.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)

# ---- (A) shipped configs still declare their drop-in directory -------------
#
# These .DIST files are the shipped config *sources*. A build tree or an
# installed package may not carry them, so gate the whole section on the
# source tree being present (xymond/etcfiles). Once it is, every expected
# declaration MUST be there: a deleted file or a commented-out directive is a
# regression to catch, not a per-file skip.
[ -d "$ROOT/xymond/etcfiles" ] || skip "shipped config sources absent (xymond/etcfiles) -- not a source tree"

# name -> shipped file. The directive is "optional directory
# @XYMONHOME@/etc/<name>.d", except the two configs whose basename already
# carries .cfg keep it in the directory name (xymonserver.d,
# xymonclient.d) -- so assert per-file on its own expected suffix.
check_declares() {  # check_declares <file> <expected-dir-suffix>
	local f=$1 suffix=$2 re
	# A sparse checkout (e.g. server-only, no client/) may lack a whole
	# subtree; only require files whose directory is actually present, so such
	# a tree skips them rather than failing. Within a present directory the
	# file MUST exist.
	[ -d "$(dirname "$f")" ] || return 0
	assert_file_exists "$f" "shipped config $(basename "$f") is missing (#222)"
	# Match an ACTIVE, whole-line declaration: a commented "# optional
	# directory ..." (or a fragment on another line) must not satisfy the
	# guard. Escape the '.' in the suffix so it stays literal in the ERE --
	# otherwise "alerts.d" would also match "alertsXd".
	re=$(printf '%s' "$suffix" | sed 's/[.]/\\./g')
	grep -qE "^[[:space:]]*optional directory @XYMONHOME@/etc/${re}[[:space:]]*\$" "$f" \
		|| fail "shipped $(basename "$f") has no active 'optional directory @XYMONHOME@/etc/$suffix' line (#222)"
}

check_declares "$ROOT/xymond/etcfiles/alerts.cfg.DIST"          "alerts.d"
check_declares "$ROOT/xymond/etcfiles/analysis.cfg.DIST"        "analysis.d"
check_declares "$ROOT/xymond/etcfiles/combo.cfg.DIST"           "combo.d"
check_declares "$ROOT/xymond/etcfiles/graphs.cfg.DIST"          "graphs.d"
check_declares "$ROOT/xymond/etcfiles/hosts.cfg.DIST"           "hosts.d"
check_declares "$ROOT/xymond/etcfiles/client-local.cfg.DIST"    "client-local.d"
check_declares "$ROOT/xymond/etcfiles/rrddefinitions.cfg.DIST"  "rrddefinitions.d"
check_declares "$ROOT/xymond/etcfiles/xymonserver.cfg.DIST"     "xymonserver.d"
check_declares "$ROOT/client/clientlaunch.cfg.DIST"             "clientlaunch.d"
check_declares "$ROOT/client/xymonclient.cfg.DIST"              "xymonclient.d"

# ---- (B) the directive actually works, via the real stackio reader ---------
#
# The reader is lib/stackio.c built STANDALONE, which is what xymonstackio is
# (lib/Makefile). Where there is one -- $STACKIO, as an installed package's
# tests export it, or a server build's lib/xymonstackio -- that binary is
# driven, so the package's own build is what gets checked. Otherwise the test
# builds the same reader against the client archives, which every build makes:
# xymonstackio links the server's libxymon.a, and a client build has none.

CC=${CC:-cc}
built_tree() {
	command -v "$CC" >/dev/null 2>&1 && [ -f "$ROOT/include/config.h" ] \
		&& [ -f "$(xymon_lib "$ROOT" libxymonclient.a)" ] && [ -f "$(xymon_lib "$ROOT" libxymonclientcomm.a)" ]
}

# build_stackio OUT [SOURCE...] -- the STANDALONE reader, with SOURCE linked in.
build_stackio() {
	local out=$1 pcre_libs harness_cflags harness_ldflags
	shift
	pcre_libs=${PCRELIBS:-}
	if [ -z "$pcre_libs" ] && command -v pkg-config >/dev/null 2>&1; then
		pcre_libs=$(pkg-config --libs libpcre2-8 2>/dev/null || true)
	fi
	[ -n "$pcre_libs" ] || pcre_libs="-lpcre2-8"
	harness_cflags=$(xymon_cflags "$ROOT")
	harness_ldflags=$(xymon_ldflags "$ROOT")
	# The client archives use PCRE only in a localclient tree, by the same
	# CLIENTONLY/LOCALCLIENT test lib/loadalerts.h makes, so a plain client tree
	# links none -- and may have none installed.
	case $harness_cflags in
		*-DLOCALCLIENT*) ;;
		*-DCLIENTONLY*) pcre_libs= ;;
	esac
	# shellcheck disable=SC2086 # the flag strings are intentionally word-split
	"$CC" -DSTANDALONE $harness_cflags -o "$out" \
		"$@" "$ROOT/lib/stackio.c" "$(xymon_lib "$ROOT" libxymonclientcomm.a)" "$(xymon_lib "$ROOT" libxymonclient.a)" \
		$harness_ldflags $pcre_libs 2>"$work/cc.log" \
		|| { cat "$work/cc.log" >&2; fail "standalone stackio does not compile"; }
}

work=$(mktempdir)

if [ -n "${STACKIO:-}" ]; then
	[ -x "$STACKIO" ] || fail "STACKIO is set to '$STACKIO', which is not an executable"
	stackio=$STACKIO
elif [ -x "$ROOT/lib/xymonstackio" ]; then
	stackio=$ROOT/lib/xymonstackio
elif built_tree; then
	stackio=$work/stackio
	build_stackio "$stackio"
else
	pass_partial "shipped configs declare their drop-in directories (#222)" "stackio mechanism check needs xymonstackio or a built tree"
fi

# The STANDALONE reader reads argv[1] via stackfgets() (which expands include
# directives), prints every line, then prompts on stdin -- feed it '.' to quit.
render() { printf '.\n' | "$stackio" "$1" 2>"$work/err.log"; }

mkdir -p "$work/frags"
printf 'FRAGMENT_ALPHA\n' >"$work/frags/10-alpha.cfg"
printf 'FRAGMENT_BETA\n'  >"$work/frags/20-beta.cfg"

# Present directory: both fragments are merged into the stream.
printf 'BASELINE_LINE\noptional directory %s\n' "$work/frags" >"$work/present.cfg"
out=$(render "$work/present.cfg")
assert_contains "BASELINE_LINE"   "$out" "stackio dropped the base config line"
assert_contains "FRAGMENT_ALPHA"  "$out" "'optional directory' did not merge a drop-in fragment (#222)"
assert_contains "FRAGMENT_BETA"   "$out" "'optional directory' merged only the first fragment (#222)"

# Missing directory + 'optional': silently ignored, no WARNING.
printf 'BASELINE_LINE\noptional directory %s/absent\n' "$work" >"$work/missing.cfg"
render "$work/missing.cfg" >/dev/null
assert_not_contains "WARNING" "$(cat "$work/err.log")" \
	"a missing 'optional directory' warned instead of being silently ignored (#222)"

# Contrast: without 'optional', a missing directory DOES warn -- proves the
# silence above is the 'optional' keyword doing its job, not a dead code path.
printf 'BASELINE_LINE\ndirectory %s/absent\n' "$work" >"$work/missing-required.cfg"
render "$work/missing-required.cfg" >/dev/null
assert_contains "WARNING" "$(cat "$work/err.log")" \
	"a missing non-optional 'directory' unexpectedly stayed silent -- the optional contrast is meaningless"

# A drop-in is read to its end before the next one starts, whatever it pulls
# in itself (#881). The whole output is compared, because the defect is order:
# every line was there, but the next drop-in was read in the middle of the
# one before it. sed stops at the blank line the reader prints before its
# prompt.
config_lines() { render "$1" | sed '/^$/,$d' | tr '\n' ' '; }

mkdir -p "$work/inc-frags" "$work/dir-frags" "$work/nested"
printf 'INNER\n' >"$work/inner.cfg"
printf 'BEFORE_A\ninclude %s\nAFTER_A\n' "$work/inner.cfg" >"$work/inc-frags/10-a.cfg"
printf 'BETA\n' >"$work/inc-frags/20-b.cfg"
printf 'BASELINE\ndirectory %s\nTAIL\n' "$work/inc-frags" >"$work/inc-in-frag.cfg"
assert_equal "BASELINE BEFORE_A INNER AFTER_A BETA TAIL " "$(config_lines "$work/inc-in-frag.cfg")" \
	"an include inside a drop-in let the next drop-in in before the rest of the including one (#881)"

# The same for a drop-in that has a directory of its own: its fragments, then
# the rest of it, then the next drop-in.
printf 'X\n' >"$work/nested/x.cfg"
printf 'Y\n' >"$work/nested/y.cfg"
printf 'A1\ndirectory %s\nA2\n' "$work/nested" >"$work/dir-frags/10-a.cfg"
printf 'B\n' >"$work/dir-frags/20-b.cfg"
printf 'BASE\ndirectory %s\nTAIL\n' "$work/dir-frags" >"$work/dir-in-frag.cfg"
assert_equal "BASE A1 X Y A2 B TAIL " "$(config_lines "$work/dir-in-frag.cfg")" \
	"a directory inside a drop-in let the next drop-in in before the rest of the one holding it (#881)"


# ---- (C) a queued drop-in that cannot be opened ---------------------------
#
# Root opens every file, and the lanes run as root, so the reader is built a
# second time with config-include-dir-harness.c, whose fopen() refuses any
# name containing "unopenable". A probe checks first that it is the fopen()
# the reader calls: where the C library's is called instead, a probe file
# that should be refused is read, and this part cannot run.
built_tree || pass_partial "drop-ins are read one at a time, and the shipped drop-in directories are declared (#222, #881)" \
	"a drop-in that cannot be opened is checked through a reader built here, and this tree is not built"
build_stackio "$work/stackio-unopenable" "$ROOT/tests/libxymon/config-include-dir-harness.c"
unopenable_lines() { printf '.\n' | "$work/stackio-unopenable" "$1" 2>"$work/err.log" | sed '/^$/,$d' | tr '\n' ' '; }

printf 'PROBE_READ\n' >"$work/unopenable-probe.cfg"
printf 'include %s\n' "$work/unopenable-probe.cfg" >"$work/probe.cfg"
case $(unopenable_lines "$work/probe.cfg") in
	*PROBE_READ*)
		pass_partial "drop-ins are read one at a time, and the shipped drop-in directories are declared (#222, #881)" \
			"the refusing fopen() is not the one stackio calls here, so a drop-in that cannot be opened is not checked" ;;
esac

# The first queued file cannot be opened: it is skipped with a warning, and
# the next one is read in its place, where the directory line stands.
mkdir -p "$work/first-bad"
printf 'A\n' >"$work/first-bad/10-unopenable.cfg"
printf 'B\n' >"$work/first-bad/20-b.cfg"
printf 'C\n' >"$work/first-bad/30-c.cfg"
printf 'BASE\ndirectory %s\nTAIL\n' "$work/first-bad" >"$work/first-bad.cfg"
assert_equal "BASE B C TAIL " "$(unopenable_lines "$work/first-bad.cfg")" \
	"a first drop-in that cannot be opened was not skipped for the next one"
assert_contains "Cannot open include file '$work/first-bad/10-unopenable.cfg'" "$(cat "$work/err.log")" \
	"a drop-in that cannot be opened was skipped without a warning"
# The warning quotes the line that queued it, as text.
assert_contains "line was: directory $work/first-bad" "$(cat "$work/err.log")" \
	"the warning for a drop-in that cannot be opened did not quote the directory line"

# A later queued file cannot be opened, as the one before it ends: it is
# skipped too, and the rest of the configuration is still read.
mkdir -p "$work/later-bad"
printf 'A\n' >"$work/later-bad/10-a.cfg"
printf 'B\n' >"$work/later-bad/20-unopenable.cfg"
printf 'C\n' >"$work/later-bad/30-c.cfg"
printf 'BASE\ndirectory %s\nTAIL\n' "$work/later-bad" >"$work/later-bad.cfg"
assert_equal "BASE A C TAIL " "$(unopenable_lines "$work/later-bad.cfg")" \
	"a later drop-in that cannot be opened ended the configuration there"
assert_contains "Cannot open include file '$work/later-bad/20-unopenable.cfg'" "$(cat "$work/err.log")" \
	"a later drop-in that cannot be opened was skipped without a warning"
# No line is being read there: the buffer still holds the last line of the
# file that ended, and quoting it would point at the wrong line.
assert_not_contains "line was" "$(cat "$work/err.log")" \
	"the warning for a later drop-in quoted a line that has nothing to do with it"

pass "shipped configs declare drop-in dirs and 'optional directory' merges fragments / tolerates absence (#222), one drop-in at a time, skipping one that cannot be opened (#881)"
