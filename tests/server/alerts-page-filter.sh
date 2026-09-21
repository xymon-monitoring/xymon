#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/alerts-page-filter.sh
#
# Regression guard for PAGE=/ in alerts.cfg -- the front page being reachable by
# name from an alert rule, including for a host that is also on a named page.
#
# criteriamatch() (lib/loadalerts.c) has named the top-level page "/" since
# 4abd6f3e0 (2016), because its pagepath is the empty string and an empty needle
# matches nothing. What it could not repair is the list it was handed:
# XMH_ALLPAGEPATHS built the comma-separated pagepath list by appending each
# path raw, and an empty element cannot be told from no element -- it either
# vanished (the separator is only written for a non-empty buffer) or left a
# trailing comma a tokeniser drops. A host listed on the front page *and* on a
# named page therefore reported only the named one, and no PAGE=/ rule could
# ever reach it. alerts.cfg(5) documents "/" as that page's name with no such
# exception.
#
# Driven through the real binary rather than a harness: "xymond_alert --test"
# runs the same criteriamatch() the daemon does, and prints the recipients a
# given host/test/colour would alert.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
# An absent in-tree binary is a skip; an explicitly exported XYMOND_ALERT that
# points at nothing is a failure, because that caller asserted the layout.
require_bin XYMOND_ALERT xymond/xymond_alert
ALERT=$XYMOND_ALERT

work=$(mktempdir)

# fronthost is on the top-level page only; bothhost is on the top-level page and
# on "home"; homehost is on "home" only.
cat >"$work/hosts.cfg" <<'EOF'
127.0.0.1  fronthost  # conn
127.0.0.8  bothhost   # conn

page home Home
127.0.0.2  homehost   # conn
127.0.0.8  bothhost   # conn
EOF

cat >"$work/alerts.cfg" <<'EOF'
PAGE=/
	MAIL frontpage@example.com
EOF

# --test resolves the host from HOSTSCFG itself (load_hostnames() in
# xymond_alert.c), so the answer does not depend on whether a xymond is running
# here; its attempt to reach one is noise on stderr. Failing to run must still
# fail the test: an empty answer satisfies every assert_not_contains below, so
# it would read as a pass.
# Sets $got rather than printing, and is called as a statement rather than in a
# command substitution: fail() exits, and an exit inside $(...) ends only the
# subshell -- the script would carry on and report a crashed binary as an
# assertion mismatch, or as a pass.
recipients() {
	got=$(env XYMONHOME="$work" XYMONTMP="$work" HOSTSCFG="$work/hosts.cfg" \
	    MACHINE="$1" MACHINEDOTS="$1" \
	    XYMONSERVERS=127.0.0.1 XYMSRV=127.0.0.1 XYMON=/bin/true \
	    "$ALERT" --config="$2" --test "$1" conn --color=red 2>"$work/alert.log") \
	    || { cat "$work/alert.log" >&2; fail "xymond_alert --test failed for $1 with ${2##*/}"; }
}

recipients fronthost "$work/alerts.cfg"
assert_contains "frontpage@example.com" "$got" \
	"PAGE=/ did not reach a host on the top-level page, which alerts.cfg.5 documents as its name"
recipients bothhost "$work/alerts.cfg"
assert_contains "frontpage@example.com" "$got" \
	"PAGE=/ did not reach a host that is on the top-level page as well as home: its front-page membership is being dropped from the pagepath list"
recipients homehost "$work/alerts.cfg"
assert_not_contains "frontpage@example.com" "$got" \
	"PAGE=/ reached a host that is only on the home page"

# Positive control for the other side of the same list: a run in which no PAGE=
# rule matched anything would leave the negative assertion above green.
cat >"$work/named.cfg" <<'EOF'
PAGE=home
	MAIL homepage@example.com
EOF

recipients homehost "$work/named.cfg"
assert_contains "homepage@example.com" "$got" \
	"PAGE=home did not reach the host it names"
recipients bothhost "$work/named.cfg"
assert_contains "homepage@example.com" "$got" \
	"PAGE=home did not reach a host whose pagepath list is /,home: only the first element is being compared"
recipients fronthost "$work/named.cfg"
assert_not_contains "homepage@example.com" "$got" \
	"PAGE=home reached a host that is only on the top-level page"

pass "alerts.cfg PAGE= selects the top-level page by name, and the named pages beside it in one host's list"
