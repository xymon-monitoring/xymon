#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/web/hostgraphs-html.sh
#
# hostgraphs.cgi, the Metrics Report, draws each graph of several hosts with
# an <img src> link to showgraph. Its parameters are joined with "&amp;",
# as an HTML attribute needs; "graph=custom" had a bare "&" before it.
#
# Needs a built tree: web/hostgraphs.cgi.
set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin HOSTGRAPHS web/hostgraphs.cgi
require_cfg XYMONSERVER_CFG xymond/etcfiles/xymonserver.cfg

work=$(mktempdir)

form='hostname=alpha&hostname=beta&testname=cpu&DoReport=Generate'
out=$(printf '%s' "$form" | REQUEST_METHOD=POST CONTENT_TYPE=application/x-www-form-urlencoded \
	CONTENT_LENGTH=${#form} XYMONHOME="$work" \
	"$HOSTGRAPHS" --env="$XYMONSERVER_CFG" 2>"$work/stderr" || true)

assert_contains 'showgraph.sh?host=alpha,beta&amp;service=cpu' "$out" "no graph link for the two hosts: $(cat "$work/stderr")"
assert_contains '&amp;graph=custom&amp;' "$out" "graph=custom is not joined with &amp;"
assert_not_contains '&graph=' "$out" "a bare & is left in the graph link"

pass "hostgraphs.cgi joins every parameter of its graph links with &amp;"
