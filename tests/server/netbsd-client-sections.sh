#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

NETBSD_CLIENT=$(find_root)/xymond/client/netbsd.c
source=$(<"$NETBSD_CLIENT")

assert_contains 'msgsstr = getdata("msgs");' "$source" \
	"NetBSD handler reads the msgs section"
assert_not_contains 'getdata("msgsstr")' "$source" \
	"NetBSD handler does not look up the variable name as a section"
assert_contains 'msgs_report(hostname, clienttype, os, hinfo, fromline, timestr, msgsstr);' "$source" \
	"NetBSD handler forwards the msgs section to msgs_report"

pass "NetBSD server client-section mappings"