#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/alerts-page-undecided.sh
#
# A PAGE= rule in alerts.cfg must find its page before it fires -- issue #529.
#
# criteriamatch() (lib/loadalerts.c) tokenises the alert's location and decides
# the filter by asking whether a token *failed* to match:
#
#     pgmatchres = pgexclres = -1;
#     pgtok = strtok(pgnames, ",");
#     while (pgtok) { ... }
#     if (pgmatchres == 0) return 0;
#
# When the loop body never runs, pgmatchres keeps its -1 sentinel, -1 is not 0,
# and the rule is accepted without any comparison having happened. "Not compared
# at all" passed for "matched", and every PAGE=-qualified rule in alerts.cfg
# fired for that alert.
#
# The empty location is not how you reach it -- criteriamatch() maps "" to "/"
# before tokenising, which is the top-level page's name. What reaches it is a
# location that is non-empty and still yields no token: strtok() skips runs of
# separators, so "," and ",," tokenise to nothing at all.
#
# The same shape of defect exists on the analysis.cfg side, in ruleset()
# (xymond/client_config.c), where an unnamed top-level page produces no token and
# every PAGE= rule therefore applies to every front-page host. That is issue #526
# and a separate change; nothing here fixes it, and this comment deliberately
# does not state what that file currently contains, because it is in flight.
#
# criteriamatch() is static, so the probe goes through next_recipient(), and the
# alert's location comes from argv rather than from a host: no hosts.cfg can
# express the location under test, which is the point.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
here=$(dirname "$0")

require_c_buildenv "$ROOT"
[ -f "$ROOT/Makefile" ] || skip "tree not configured (no Makefile)"
[ -f "$ROOT/lib/libxymoncomm.a" ] \
	|| skip "tree not built (run make first; the post-build CI suite covers this)"

work=$(mktempdir)
CC=${CC:-cc}

build_xymon_libs "$ROOT" "$work/libbuild.log" libxymoncomm.a

# PCRELIBS: explicit env override first, then the configured tree's own value
# (which carries any -L the platform needs), then pkg-config, then a bare -l.
pcre_libs=${PCRELIBS:-}
[ -n "$pcre_libs" ] || [ ! -f "$ROOT/Makefile" ] \
	|| pcre_libs=$(sed -n 's/^PCRELIBS *= *//p' "$ROOT/Makefile")
if [ -z "$pcre_libs" ] && command -v pkg-config >/dev/null 2>&1; then
	pcre_libs=$(pkg-config --libs libpcre2-8 2>/dev/null || true)
fi
[ -n "$pcre_libs" ] || pcre_libs="-lpcre2-8"

harness_cflags=$(xymon_cflags "$ROOT")
harness_ldflags=$(xymon_ldflags "$ROOT")
# shellcheck disable=SC2086  # deliberate word-splitting, as the neighbouring tests do
"$CC" $harness_cflags -o "$work/harness" \
	"$here/alerts-page-undecided-harness.c" \
	"$ROOT/lib/libxymoncomm.a" $harness_ldflags $pcre_libs 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; fail "harness does not compile"; }

cat >"$work/hosts.cfg" <<'EOF'
127.0.0.1 testhost # conn
EOF

cat >"$work/alerts.cfg" <<'EOF'
PAGE=somepage
	MAIL page-rule@example.com
EOF

# probe LOCATION [ALERTS.CFG] -- set $got to the recipient count the alert
# reaches. Not a command substitution at the call sites: fail() exits, and an
# exit inside $(...) ends only the subshell -- the script would carry on and
# report a missing count as an assertion mismatch, or pass outright.
probe() {
	local cfg=${2:-$work/alerts.cfg}
	got=$("$work/harness" "$work/hosts.cfg" "$cfg" "$1" 2>"$work/run.log" \
		| sed -n 's/^recipients=//p') \
		|| { cat "$work/run.log" >&2; fail "harness run failed: location '$1', ${cfg##*/}"; }
	[ -n "$got" ] \
		|| { cat "$work/run.log" >&2; fail "harness printed no count: location '$1', ${cfg##*/}"; }
}

# A rule with no PAGE= filter at all must be untouched by any of this. It is the
# control that matters most: the guard is `crit->pagespec && pgmatchres != 1`,
# and dropping the pagespec half rejects every rule that does not name a page --
# which is most of an ordinary alerts.cfg. Verified: without that half this
# fixture yields no recipient at all.
cat >"$work/nopage.cfg" <<'EOF'
HOST=testhost
	MAIL nopage@example.com
EOF

probe somepage "$work/nopage.cfg"
assert_equal "1" "$got" \
	"a rule with no PAGE= filter stopped firing: the page test is being applied to rules that do not name a page"

# Controls first: without them a fix that rejected everything would look green.
probe somepage
assert_equal "1" "$got" \
	"PAGE=somepage did not reach an alert on the page it names"
probe otherpage
assert_equal "0" "$got" \
	"PAGE=somepage reached an alert on a different page"
probe ''
assert_equal "0" "$got" \
	"PAGE=somepage reached an alert on the top-level page, which criteriamatch() names /"

# The defect. strtok() skips runs of separators, so a location can be non-empty
# and still yield no token at all: these two are what reach the sentinel.
probe ,
assert_equal "0" "$got" \
	"PAGE=somepage fired for a location that yields no token: an undecided page filter is being treated as a match"
probe ,,
assert_equal "0" "$got" \
	"PAGE=somepage fired for a location of ',,': an undecided page filter is being treated as a match"

# Not the sentinel: "one," does yield a token, so this walks the ordinary
# mismatch path. It is the boundary between the two, not a second copy of them.
probe 'one,'
assert_equal "0" "$got" \
	"PAGE=somepage fired for a location whose only token is a different page"

# The "" -> "/" mapping at lib/loadalerts.c:916 is what keeps the top-level page
# matchable at all, and every assertion above stays green without it -- an empty
# location would simply yield no token and be rejected, which is the 0 they
# already expect. This is the one that notices.
cat >"$work/frontpage.cfg" <<'EOF'
PAGE=/
	MAIL frontpage@example.com
EOF

probe '' "$work/frontpage.cfg"
assert_equal "1" "$got" \
	"PAGE=/ no longer reaches an alert on the top-level page: the empty location is not being named /"

# What this change gives up, asserted rather than left implicit. Before, a
# token-less location matched PAGE=/ -- but only because it matched every PAGE=
# rule, that one included. "" is the top-level page and still matches; "," names
# no page at all and now matches no page rule. A rule with no PAGE= filter is
# unaffected and still fires for such an alert, which is what keeps this from
# being silence: measured, "," against PAGE=/ plus an unfiltered rule goes from
# two recipients to one, not to none.
probe , "$work/frontpage.cfg"
assert_equal "0" "$got" \
	"PAGE=/ matched a location that names no page at all"

# The exclusion half of the same block, which already had the shape this gives
# the include half: EXPAGE= drops the rule only on a positive match, so a
# location it cannot decide is not excluded. Untouched by this change, and
# asserted so that a later edit to one half cannot silently invert the other.
cat >"$work/expage.cfg" <<'EOF'
EXPAGE=somepage
	MAIL expage@example.com
EOF

probe somepage "$work/expage.cfg"
assert_equal "0" "$got" \
	"EXPAGE=somepage did not exclude an alert on the page it names"
probe otherpage "$work/expage.cfg"
assert_equal "1" "$got" \
	"EXPAGE=somepage excluded an alert on a different page"
probe , "$work/expage.cfg"
assert_equal "1" "$got" \
	"EXPAGE=somepage excluded an alert whose location yields no token: an undecided exclusion must not exclude"

pass "alerts.cfg page filters decide on a match, not on the absence of a failure (#529)"
