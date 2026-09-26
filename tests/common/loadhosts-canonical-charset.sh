#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/common/loadhosts-canonical-charset.sh
#
# Behavioural test of the BUILT xymongrep binary, exercising the real
# load_hostnames() path in libxymon. A canonical hostname (field 2 of
# hosts.cfg) outside the character set the web CGIs serve
# (XYMON_HOSTNAME_CHARS, shared with lib/cgi.c and the xymond ghost-name
# guard) is still loaded and monitored -- it is NOT dropped: dropping it
# would remove it from the roster, falling out of alerting and letting
# trimhistory(1) delete its history as orphaned (issue #309).
#
# The "your host is web-unreachable" warning is emitted once per config load
# by xymond, NOT by the shared loader -- the loader runs in every short-lived
# CGI process, so warning there would repeat on every page render. This test
# pins both halves: xymongrep (a loader client) loads every host AND stays
# silent; it does not emit the warning.
#
# A space cannot reach field 2 (the parser's "%s" stops at whitespace), so it
# is not a case here; the reachable violations are punctuation and non-ASCII.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

default="common/xymongrep"
if [ -z "${XYMONGREP:-}" ] && [ ! -x "$(find_root)/$default" ] \
		&& [ -x "$(find_root)/client/xymongrep" ]; then
	default="client/xymongrep"
fi
require_bin XYMONGREP "$default"

work=$(mktempdir)
# Every host is tagged "cpu" so one grep lists all that load. raksmorgas keeps
# an ASCII canonical name and its non-ASCII spelling in NAME:.
cat >"$work/hosts.cfg" <<'EOF'
1.2.3.4		goodhost		# cpu
1.2.3.5		host.example.com	# cpu
1.2.3.6		fe80-host		# cpu
1.2.3.7		host_01			# cpu
1.2.3.8		raksmorgas		# NAME:"räksmörgås" cpu
1.2.3.9		host(1)			# cpu
1.2.3.10	bad!host		# cpu
1.2.3.11	räkserver		# cpu
EOF

# --hosts= keeps the test hermetic (no xymond needed).
out=$("$XYMONGREP" --hosts="$work/hosts.cfg" cpu 2>"$work/err")
err=$(cat "$work/err")

# Every host loads and is selectable -- the well-formed names AND the ones with
# characters the web cannot serve: the loader keeps them, it does not drop them.
assert_contains "goodhost # cpu"         "$out" "plain ASCII name loads"
assert_contains "host.example.com # cpu" "$out" "FQDN (dots) loads"
assert_contains "fe80-host # cpu"        "$out" "hyphenated name loads"
assert_contains "host_01 # cpu"          "$out" "underscore name loads"
assert_contains "raksmorgas # cpu"       "$out" "ASCII canonical with a non-ASCII NAME: loads"
assert_contains "host(1) # cpu"          "$out" "punctuation name is still monitored, not dropped"
assert_contains "bad!host # cpu"         "$out" "punctuation name is still monitored, not dropped"
assert_contains "räkserver # cpu"        "$out" "non-ASCII name is still monitored, not dropped"

# The shared loader stays silent: the web-unreachability warning is emitted by
# xymond once per config load, not by every CGI/xymongrep run.
assert_not_contains "not reachable"    "$err" "the loader does not warn (warning is daemon-only)"
assert_not_contains "Warning: hostname" "$err" "the loader emits no per-load hostname warning"

pass "loadhosts keeps web-unreachable canonical hostnames and stays silent (#309)"
