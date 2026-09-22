#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/web/webaccess-toppage.sh
#
# Page-based web access must be grantable for hosts on the top-level page --
# issue #535.
#
# web_access_allowed() (lib/webaccess.c) walks the host's pagepath list and
# reduces each element to its top-level component before looking up
# "<component> <username>", so that a group named for a page also covers the
# pages below it: "sub/deep" is granted by "sub".
#
# The top-level page is the exception, because its name *is* the separator.
# XMH_ALLPAGEPATHS reports it as "/", and a subpage declared above any page line
# lands under it as "/name". Truncating either at its leading slash leaves the
# empty string, and no line of the access config can name an empty group -- so
# the key was never found and no user could be granted access to a front-page
# host at all. The only way in was "root:", which grants everything.
#
# Before #526 was fixed the same hosts were unreachable by a different route: the pagepath
# list was the empty string, strtok() returned nothing, and the loop body never
# ran. So this could not be fixed until the page had a name.
#
# The fix makes the top-level element of a slash-leading pagepath end at the
# *next* separator: "/" stays "/", and "/name/deep" becomes "/name". Page
# inheritance then works the same way everywhere -- "sub" covers "sub/deep",
# "/early" covers "/early/deeper" -- and a group named "/" reaches the front page
# only. Reducing "/name" to "/" instead would let a group named for the front
# page reach a page it was never named on, because "/name" is also what a page
# whose name begins with a slash produces and the two are the same string. All
# of those directions are asserted below.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
here=$(dirname "$0")

[ -f "$ROOT/lib/webaccess.c" ] || skip "lib/webaccess.c not present in this checkout"

require_c_buildenv "$ROOT"
[ -f "$ROOT/Makefile" ] || skip "tree not configured (no Makefile)"
[ -f "$ROOT/lib/libxymoncomm.a" ] \
	|| skip "tree not built (run make first; the post-build CI suite covers this)"

work=$(mktempdir)
CC=${CC:-cc}

build_xymon_libs "$ROOT" "$work/libbuild.log" libxymoncomm.a

pcre_libs=${PCRELIBS:-}
[ -n "$pcre_libs" ] || pcre_libs=$(sed -n 's/^PCRELIBS *= *//p' "$ROOT/Makefile")
if [ -z "$pcre_libs" ] && command -v pkg-config >/dev/null 2>&1; then
	pcre_libs=$(pkg-config --libs libpcre2-8 2>/dev/null || true)
fi
[ -n "$pcre_libs" ] || pcre_libs="-lpcre2-8"

harness_cflags=$(xymon_cflags "$ROOT")
harness_ldflags=$(xymon_ldflags "$ROOT")
# shellcheck disable=SC2086  # deliberate word-splitting, as the neighbouring tests do
"$CC" $harness_cflags -o "$work/harness" \
	"$here/webaccess-toppage-harness.c" \
	"$ROOT/lib/libxymoncomm.a" $harness_ldflags $pcre_libs 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; fail "harness does not compile"; }

# fronthost is on the top-level page. earlyhost is on a subpage declared before
# any page line, so its pagepath begins with the separator. subhost and deephost
# are on a named page and a page below it.
# The fixture covers both routes to a slash-leading pagepath and both routes to
# a nested one. A subpage declared above any page line lands under the top page
# as "/early"; a page whose name begins with a slash gives "/admin"; the two are
# the same shape of string and cannot be told apart. Each has a child, so
# inheritance is exercised on the branch this change touches as well as on the
# ordinary "sub/deep" one. bothhost is on the top page and a named page at once,
# so its list has more than one element.
cat >"$work/hosts.cfg" <<'EOF'
127.0.0.1 fronthost # conn
127.0.0.5 bothhost # conn

subpage early Early
127.0.0.4 earlyhost # conn

subparent early deeper Deeper
127.0.0.7 deeperhost # conn

page /admin Admin
127.0.0.6 adminhost # conn

subpage adsub AdSub
127.0.0.8 adsubhost # conn

page sub Subpage
127.0.0.2 subhost # conn
127.0.0.5 bothhost # conn

subpage deep Deep
127.0.0.3 deephost # conn
EOF

cat >"$work/access.cfg" <<'EOF'
/: alice
sub: bob
/early: erin
/admin: gina
fronthost: harry
EOF

hosts="fronthost bothhost earlyhost deeperhost adminhost adsubhost subhost deephost"

probe() {
	# shellcheck disable=SC2086  # $hosts is a deliberate word list
	got=$("$work/harness" "$work/hosts.cfg" "$work/access.cfg" "$1" $hosts \
		2>"$work/run.log") \
		|| { cat "$work/run.log" >&2; fail "harness run failed for user '$1'"; }
}
verdict() { printf '%s\n' "$got" | sed -n "s/^$1=[^=]*=//p"; }
paths() { printf '%s\n' "$got" | sed -n "s/^$1=\\([^=]*\\)=.*/\\1/p"; }

probe alice

assert_equal "/" "$(paths fronthost)" \
	"fixture is wrong: the top-level page must report its pagepath as /"
assert_equal "/early" "$(paths earlyhost)" \
	"fixture is wrong: a subpage above any page line must land under the top page"
assert_equal "/admin" "$(paths adminhost)" \
	"fixture is wrong: a page named with a leading slash must keep it in its pagepath"
assert_equal "/,sub" "$(paths bothhost)" \
	"fixture is wrong: bothhost must be on the top-level page and on a named one"

# The defect. Before the fix these were 0, and "root:" was the only way to grant
# a front-page host to anyone.
assert_equal "1" "$(verdict fronthost)" \
	"a group named / does not grant access to a host on the top-level page (#535)"
assert_equal "1" "$(verdict bothhost)" \
	"a group named / does not grant access to a host on the top-level page and a named one (#535)"

# The other direction, and the reason only the exact name is exempted. "/admin"
# is what a page named with a leading slash produces, and it is the same string
# a subpage above any page line produces -- so a rule that reduced either to "/"
# would grant a page the group was never named on.
assert_equal "0" "$(verdict adminhost)" \
	"a group named / granted access to a page merely named with a leading slash (#535)"
assert_equal "0" "$(verdict earlyhost)" \
	"a group named / granted access to a page below the top-level one, which it is not named for (#535)"
assert_equal "0" "$(verdict deeperhost)" \
	"a group named / granted access to a page two below the top-level one (#535)"
assert_equal "0" "$(verdict adsubhost)" \
	"a group named / granted access to a page below one merely named with a leading slash (#535)"
assert_equal "0" "$(verdict subhost)" \
	"a group named / granted access to a host on a named page (#535)"
assert_equal "0" "$(verdict deephost)" \
	"a group named / granted access to a host on a page below a named one (#535)"

probe bob

# Unchanged behaviour: a group named for a page still covers the pages below it,
# and still does not reach the top-level page.
assert_equal "1" "$(verdict subhost)" \
	"a group named sub stopped granting access to the page it names"
assert_equal "1" "$(verdict deephost)" \
	"a group named sub stopped covering sub/deep, which it has always covered"
assert_equal "0" "$(verdict fronthost)" \
	"a group named sub granted access to the top-level page"
assert_equal "1" "$(verdict bothhost)" \
	"a group named sub stopped reaching a host that is also on the top-level page"

# Each page below the top one is grantable by the name it has everywhere else,
# which is the half that fails if those pagepaths are reduced to "/" instead of
# being left whole.
probe erin
assert_equal "1" "$(verdict earlyhost)" \
	"a group named /early does not grant the page it names (#535)"
assert_equal "1" "$(verdict deeperhost)" \
	"a group named /early does not cover the page below it, though sub covers sub/deep (#535)"
assert_equal "0" "$(verdict fronthost)" \
	"a group named /early granted access to the top-level page (#535)"

probe gina
assert_equal "1" "$(verdict adminhost)" \
	"a group named /admin does not grant the page it names (#535)"
assert_equal "1" "$(verdict adsubhost)" \
	"a group named /admin does not cover the page below it (#535)"
assert_equal "0" "$(verdict fronthost)" \
	"a group named /admin granted access to the top-level page (#535)"

# The other two ways this function grants access, neither of which goes near the
# pagepath: a group named for the host, and "root:". Asserted because the page
# reduction is the only thing that changed and they must not move with it.
probe harry
assert_equal "1" "$(verdict fronthost)" \
	"a group named for a host stopped granting that host"
assert_equal "0" "$(verdict subhost)" \
	"a group named for one host granted another"

pass "page-based web access reaches the top-level page, and stays inside the page it names (#535)"
