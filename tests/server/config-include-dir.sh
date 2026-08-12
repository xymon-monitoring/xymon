#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/config-include-dir.sh
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
#       (lib/stackio.c) built STANDALONE, with a present dir carrying two
#       fragments and, for contrast, a missing dir with and without 'optional'.

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
# carries .cfg keep it in the directory name (xymonserver.cfg.d,
# xymonclient.cfg.d) -- so assert per-file on its own expected suffix.
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
check_declares "$ROOT/xymond/etcfiles/xymonserver.cfg.DIST"     "xymonserver.cfg.d"
check_declares "$ROOT/client/clientlaunch.cfg.DIST"             "clientlaunch.d"
check_declares "$ROOT/client/xymonclient.cfg.DIST"              "xymonclient.cfg.d"

# ---- (B) the directive actually works, via the real stackio reader ---------

CC=${CC:-cc}
if ! command -v "$CC" >/dev/null 2>&1 \
	|| [ ! -f "$ROOT/include/config.h" ] || [ ! -f "$ROOT/lib/libxymoncomm.a" ]; then
	pass "shipped configs declare their drop-in directories (#222); stackio mechanism check skipped (tree not built)"
fi

work=$(mktempdir)

ssllibs=$(sed -n 's/^SSLLIBS *= *//p' "$ROOT/Makefile")
pcre_libs=${PCRELIBS:-}
if [ -z "$pcre_libs" ] && command -v pkg-config >/dev/null 2>&1; then
	pcre_libs=$(pkg-config --libs libpcre2-8 2>/dev/null || true)
fi
[ -n "$pcre_libs" ] || pcre_libs="-lpcre2-8"

harness_cflags=$(xymon_cflags "$ROOT")
"$CC" -DSTANDALONE $harness_cflags -o "$work/stackio" \
	"$ROOT/lib/stackio.c" "$ROOT/lib/libxymoncomm.a" $ssllibs $pcre_libs 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; fail "standalone stackio does not compile"; }

# The STANDALONE reader reads argv[1] via stackfgets() (which expands include
# directives), prints every line, then prompts on stdin -- feed it '.' to quit.
render() { printf '.\n' | "$work/stackio" "$1" 2>"$work/err.log"; }

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

pass "shipped configs declare drop-in dirs and 'optional directory' merges fragments / tolerates absence (#222)"
