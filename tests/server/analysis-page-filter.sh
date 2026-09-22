#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/analysis-page-filter.sh
#
# Regression guard for the PAGE=/EXPAGE= host filters in analysis.cfg, and for
# the front page having a name to be filtered by.
#
# The top-level page's pagepath is the empty string (the pagelist head in
# lib/loadhosts.c), and two things went wrong with that:
#
#   - ruleset() (xymond/client_config.c) tokenised the host's pagepath list and
#     rejected a rule when a token failed to match. On the front page there was
#     no token, so nothing was compared and the "did it fail" test was false:
#     every PAGE=-qualified rule in the file applied to every front-page host,
#     which collected a foreign page's PROC, DISK and LOAD rules and went red on
#     processes it does not run. Hosts on named pages filtered correctly, which
#     is what made the report read as "the main page ignores PAGE=".
#
#   - XMH_ALLPAGEPATHS appended that empty path raw, and an empty element in a
#     comma-separated list cannot be told from no element: a host on the front
#     page and on "home" reported "home", losing its front-page membership
#     before any consumer could match on it.
#
# A third defect sat in the same block: the pagepath list was strdup'ed once for
# the whole rule list and walked with strtok(), which writes NULs over the
# separators, so every rule after the first saw only the first pagepath. Whether
# a PAGE= rule matched a multi-page host depended on how many page-filtered
# rules preceded it in the file.
#
# A fourth sat in the caching. ruleset() keys its answer on the hostname alone,
# while a minority of its callers asked for XMH_PAGEPATH and the rest for
# XMH_ALLPAGEPATHS. Those differ on a host that is on several pages, so the
# getter that ran first decided the ruleset every later one received, and which
# runs first depends on the sections a client message happens to carry. They all
# read XMH_ALLPAGEPATHS now, which the source guard at the end of this file pins.
#
# ruleset() is static and no binary exposes it, so this compiles a harness
# against the real xymond/client_config.c and probes through two of its public
# callers, whose defaults both read 5.0 for "no rule reached this host":
# get_cpu_thresholds() (a LOAD rule) and get_paging_thresholds() (a PAGING
# rule). Both are needed, and so is running them in each order: cpu has always
# read the full list and runs first in the handlers that report a cpu section,
# so its answer is the one the rest of that report inherits, and paging is one
# of the getters that used to disagree with it. Each scenario has its own
# analysis.cfg:
#
#   page-scoped : PAGE=deltachat -> the deltachat host only. The front-page host
#                 must NOT collect it; before the fix it did.
#   front-page  : PAGE=/ -> the front-page hosts only, including the one that is
#                 also on "home". analysis.cfg.5 documents "/" as that page's
#                 name; before the fix "/" reached the front page only because
#                 *every* pattern did, and never reached the multi-page host.
#   excluded    : EXPAGE=deltachat -> everyone except the deltachat host, front
#                 page included. Guards the other side: the fix must not turn a
#                 front-page host into something EXPAGE= swallows.
#   ex-front    : EXPAGE=/ -> everyone except the front-page hosts. This is the
#                 side of the change that can newly *remove* rules from a host:
#                 before the fix no EXPAGE= pattern could exclude the front page.
#   multi-page  : two PAGE= rules, the first naming a page nobody is on. The
#                 first rule tokenises the shared list; on the old code the
#                 separators were gone by the time the second rule looked, so
#                 the two-page host no longer matched. One page-filtered rule
#                 ahead of the tested one is what it takes -- a filterless rule
#                 does not tokenise anything and would not catch this.
#   order-free  : the same two rules with the decoy removed, to show the
#                 multi-page result does not depend on what precedes it.
#   primary-page: the same page scoping seen through get_paging_thresholds(),
#                 one of the six getters that used to read the primary pagepath
#                 alone. It has to reach the same verdict as the LOAD probe.
#   primary-front: PAGE=/ through that same caller, so the front page is named
#                 for it too and not just for the eleven.
#   cache-order : both getters on one multi-page host, each order, one PAGE=
#                 rule setting both thresholds. The ruleset is cached per
#                 hostname, so a getter reading a different page item does not
#                 get its own answer -- it decides everyone else's. Priming
#                 through either getter must leave the other's verdict
#                 unchanged.
#   cache-order-ex: the same shape with EXPAGE=, the half that *removes* rules
#                 from a host and so can stop it alerting. It is the sharper of
#                 the two: a getter back on the primary pagepath both keeps a
#                 PAGING rule the exclusion should have taken away and hands
#                 that excluded rule to LOAD through the cache.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
here=$(dirname "$0")

CLIENT_CONFIG_C="$ROOT/xymond/client_config.c"
[ -f "$CLIENT_CONFIG_C" ] || skip "xymond/client_config.c not present in this checkout"

require_c_buildenv "$ROOT"
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
"$CC" $harness_cflags -iquote "$ROOT/xymond" -o "$work/harness" \
	"$here/analysis-page-filter-harness.c" "$CLIENT_CONFIG_C" \
	"$ROOT/lib/libxymoncomm.a" $harness_ldflags $pcre_libs 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; fail "harness does not compile"; }

# fronthost is on the top-level page only. bothhost is on the top-level page and
# on "home", so its pagepath list exercises the front page inside a list.
# multihost is on two named pages.
cat >"$work/hosts.cfg" <<'EOF'
127.0.0.1	fronthost	#
127.0.0.8	bothhost	#

page home Home
127.0.0.2	homehost	#
127.0.0.8	bothhost	#
127.0.0.9	multihost	#

page deltachat DeltaChat
127.0.0.3	dchost		#
127.0.0.9	multihost	#
EOF

hosts="fronthost bothhost homehost dchost multihost"

# 5.0 is get_cpu_thresholds()'s default loadyellow: "no LOAD rule matched".
probe() {
	# shellcheck disable=SC2086  # $hosts is a deliberate word list
	"$work/harness" "$work/hosts.cfg" "$1" "${2:-load}" $hosts \
		2>"$work/run.log" || { cat "$work/run.log" >&2; fail "harness run failed ($1, probe ${2:-load})"; }
}
get() { printf '%s\n' "$2" | sed -n "s/^$1=[^=]*=//p"; }
paths() { printf '%s\n' "$2" | sed -n "s/^$1=\\([^=]*\\)=.*/\\1/p"; }

cat >"$work/page-scoped.cfg" <<'EOF'
PAGE=deltachat
	LOAD 99.0 99.9
EOF
out=$(probe "$work/page-scoped.cfg")

assert_equal "/" "$(paths fronthost "$out")" \
	"the top-level page lost its name: XMH_ALLPAGEPATHS must report it as /"
assert_equal "/,home" "$(paths bothhost "$out")" \
	"a host on the front page and on home lost one of them from XMH_ALLPAGEPATHS"
assert_equal "home,deltachat" "$(paths multihost "$out")" \
	"fixture is wrong: multihost must be on two named pages"

assert_equal "5.0" "$(get fronthost "$out")" \
	"PAGE=deltachat reached a host on the top-level page: every PAGE= rule applies to the front page again"
assert_equal "5.0" "$(get bothhost "$out")" \
	"PAGE=deltachat reached a host on the front page and home"
assert_equal "5.0" "$(get homehost "$out")" \
	"PAGE=deltachat reached a host on the home page"
assert_equal "99.0" "$(get dchost "$out")" \
	"PAGE=deltachat did not reach the host it names"

cat >"$work/front-page.cfg" <<'EOF'
PAGE=/
	LOAD 77.0 77.9
EOF
out=$(probe "$work/front-page.cfg")

assert_equal "77.0" "$(get fronthost "$out")" \
	"PAGE=/ did not reach the top-level page, which analysis.cfg.5 documents as its name"
assert_equal "77.0" "$(get bothhost "$out")" \
	"PAGE=/ missed a host that is on the front page as well as home"
assert_equal "5.0" "$(get homehost "$out")" \
	"PAGE=/ reached a host on the home page"
assert_equal "5.0" "$(get dchost "$out")" \
	"PAGE=/ reached a host on the deltachat page"

cat >"$work/excluded.cfg" <<'EOF'
EXPAGE=deltachat
	LOAD 88.0 88.9
EOF
out=$(probe "$work/excluded.cfg")

assert_equal "88.0" "$(get fronthost "$out")" \
	"EXPAGE=deltachat excluded a host on the top-level page"
assert_equal "88.0" "$(get bothhost "$out")" \
	"EXPAGE=deltachat excluded a host whose pagepath list starts with the front page"
assert_equal "88.0" "$(get homehost "$out")" \
	"EXPAGE=deltachat excluded a host on the home page"
assert_equal "5.0" "$(get dchost "$out")" \
	"EXPAGE=deltachat did not exclude the host it names"

cat >"$work/ex-front.cfg" <<'EOF'
EXPAGE=/
	LOAD 66.0 66.9
EOF
out=$(probe "$work/ex-front.cfg")

assert_equal "5.0" "$(get fronthost "$out")" \
	"EXPAGE=/ did not exclude the top-level page"
assert_equal "5.0" "$(get bothhost "$out")" \
	"EXPAGE=/ did not exclude a host that is on the front page as well as home"
assert_equal "66.0" "$(get homehost "$out")" \
	"EXPAGE=/ excluded a host that is only on the home page"
assert_equal "66.0" "$(get dchost "$out")" \
	"EXPAGE=/ excluded a host on the deltachat page"

# The decoy is page-filtered, so it reaches the tokeniser and walks the list
# before the rule under test does. Sharing one buffer between rules is what this
# catches: on the old code the separators were consumed here.
cat >"$work/multi-page.cfg" <<'EOF'
PAGE=nosuchpage
	DISK * 90 95
PAGE=deltachat
	LOAD 99.0 99.9
EOF
out=$(probe "$work/multi-page.cfg")

assert_equal "99.0" "$(get multihost "$out")" \
	"PAGE=deltachat missed a host on home,deltachat once another page-filtered rule preceded it: the pagepath list is being shared between rules"
assert_equal "5.0" "$(get fronthost "$out")" \
	"PAGE=deltachat reached the top-level page when another rule preceded it"

cat >"$work/order-free.cfg" <<'EOF'
PAGE=deltachat
	LOAD 99.0 99.9
EOF
out=$(probe "$work/order-free.cfg")

assert_equal "99.0" "$(get multihost "$out")" \
	"PAGE=deltachat did not reach a host on home,deltachat even with nothing before it"

# PAGING is one of the six getters that used to read XMH_PAGEPATH, the primary
# pagepath alone. The expected values below are the LOAD probe's: that the two
# agree is the point.
cat >"$work/primary-page.cfg" <<'EOF'
PAGE=deltachat
	PAGING 33 44
EOF
out=$(probe "$work/primary-page.cfg" paging)

assert_equal "5.0" "$(get fronthost "$out")" \
	"PAGE=deltachat reached the top-level page through a XMH_PAGEPATH caller: ruleset() is not naming the front page"
assert_equal "5.0" "$(get homehost "$out")" \
	"PAGE=deltachat reached the home page through a XMH_PAGEPATH caller"
assert_equal "33.0" "$(get dchost "$out")" \
	"PAGE=deltachat did not reach the host it names through a XMH_PAGEPATH caller"

# Rejecting a foreign page is already covered by requiring a positive match, so
# *selecting* the front page is the separate thing: PAGE=/ has to name it for
# this caller as well.
cat >"$work/primary-front.cfg" <<'EOF'
PAGE=/
	PAGING 33 44
EOF
out=$(probe "$work/primary-front.cfg" paging)

assert_equal "33.0" "$(get fronthost "$out")" \
	"PAGE=/ did not reach the top-level page through a XMH_PAGEPATH caller: ruleset() is not naming the front page"
assert_equal "5.0" "$(get homehost "$out")" \
	"PAGE=/ reached the home page through a XMH_PAGEPATH caller"

# multihost is on home,deltachat, so its primary pagepath ("home") and its full
# list disagree -- the only shape in which the cache can hand one getter an
# answer built for another's question. One rule sets both thresholds, so each
# probe has something to find, and the ordered probes prime the cache through
# the other getter first. All four must agree with the unprimed pair.
cat >"$work/cache-order.cfg" <<'EOF'
PAGE=deltachat
	LOAD 99.0 99.9
	PAGING 33 44
EOF

out=$(probe "$work/cache-order.cfg" load)
assert_equal "99.0" "$(get multihost "$out")" \
	"PAGE=deltachat did not reach a host on home,deltachat through the LOAD probe"

out=$(probe "$work/cache-order.cfg" paging)
assert_equal "33.0" "$(get multihost "$out")" \
	"PAGE=deltachat did not reach a host on home,deltachat through the PAGING probe: that caller is reading the primary pagepath instead of the full list"

out=$(probe "$work/cache-order.cfg" load-after-paging)
assert_equal "99.0" "$(get multihost "$out")" \
	"PAGING ran first and left get_cpu_thresholds() a ruleset built for the primary pagepath: the two callers are asking ruleset() different questions under one cache key"

# This one does not fail if get_paging_thresholds() alone regresses -- LOAD runs
# first and caches the full-list ruleset that PAGING then inherits, which is
# exactly why the defect survived so long. It pins the other direction: that the
# inherited answer is the right one, so a regression in get_cpu_thresholds()
# cannot quietly narrow every getter that follows it.
out=$(probe "$work/cache-order.cfg" paging-after-load)
assert_equal "33.0" "$(get multihost "$out")" \
	"LOAD ran first and the ruleset it cached did not carry the PAGE=deltachat rule to get_paging_thresholds()"

# EXPAGE= is the half that removes rules. On the primary pagepath ("home") the
# exclusion misses, so a getter reading it keeps a rule it should have lost --
# and, through the cache, hands that rule to every getter that follows.
cat >"$work/cache-order-ex.cfg" <<'EOF'
EXPAGE=deltachat
	LOAD 99.0 99.9
	PAGING 33 44
EOF

out=$(probe "$work/cache-order-ex.cfg" load)
assert_equal "5.0" "$(get multihost "$out")" \
	"EXPAGE=deltachat did not exclude a host on home,deltachat through the LOAD probe"

out=$(probe "$work/cache-order-ex.cfg" paging)
assert_equal "5.0" "$(get multihost "$out")" \
	"EXPAGE=deltachat did not exclude a host on home,deltachat through the PAGING probe: that caller is matching the primary pagepath, which the exclusion does not name"

out=$(probe "$work/cache-order-ex.cfg" load-after-paging)
assert_equal "5.0" "$(get multihost "$out")" \
	"PAGING ran first, kept a rule EXPAGE=deltachat excludes, and get_cpu_thresholds() then applied it: an excluded rule reached a host through the cache"

out=$(probe "$work/cache-order-ex.cfg" paging-after-load)
assert_equal "5.0" "$(get multihost "$out")" \
	"LOAD ran first and the exclusion it cached did not reach get_paging_thresholds()"

# The probes above reach two getters; the rest hand ruleset() their page list
# the same way and would fail the same way unseen, so the shape is pinned at the
# source instead. A probe per getter would exercise the same ruleset() once more
# and say nothing new about it. check_rrdds_thresholds() is deliberately not
# covered: its page list is a parameter that arrives over the channel.
page_items=$(grep -cE '(pagename|pagepaths) = xmh_item\(hinfo,' "$CLIENT_CONFIG_C")
[ "$page_items" -gt 10 ] \
	|| fail "the page-item assignments in client_config.c no longer look like this, so the check below would pass without testing anything"

stray=$(grep -nE '(pagename|pagepaths) = xmh_item\(hinfo,' "$CLIENT_CONFIG_C" \
	| grep -v XMH_ALLPAGEPATHS || true)
assert_equal "" "$stray" \
	"a ruleset() caller reads a page item other than XMH_ALLPAGEPATHS; the ruleset is cached per hostname, so this one does not get its own answer, it decides the answer for every getter that runs after it"

pass "PAGE= and EXPAGE= filter analysis.cfg rules by pagepath, front page included and named, whichever getter asks first"
