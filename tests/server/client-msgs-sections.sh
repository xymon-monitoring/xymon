#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

CLIENT_DIR=$(find_root)/xymond/client
mapfile -t handlers < <(grep -l 'msgs_report(' "$CLIENT_DIR"/*.c | sort)
((${#handlers[@]} > 0)) || fail "no client handlers call msgs_report"

for handler in "${handlers[@]}"; do
	name=$(basename "$handler" .c)
	section=msgs
	[[ "$name" == bbwin ]] && section=msg
	source=$(<"$handler")

	assert_contains "msgsstr = getdata(\"$section\");" "$source" \
		"$name handler reads the $section section"
	assert_not_contains 'getdata("msgsstr")' "$source" \
		"$name handler does not look up the variable name as a section"
	assert_contains 'msgs_report(hostname, clienttype, os, hinfo, fromline, timestr, msgsstr);' "$source" \
		"$name handler forwards its message section to msgs_report"
done

pass "Server client message-section mappings"