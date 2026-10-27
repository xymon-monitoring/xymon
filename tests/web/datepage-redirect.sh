#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/web/datepage-redirect.sh
#
# The periodic reports form posts a day, week or month to datepage.cgi, which
# answers with a refresh to that report under its --url. Since the 2019 buffer
# fixes the URL was built with snprintf() into a buffer whose recorded size was
# never set -- malloc() instead of SBUF_MALLOC() -- so nothing was written and
# the browser was sent to whatever the buffer held, usually nowhere.
#
# Needs a built tree: web/datepage.cgi.
set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin DATEPAGE web/datepage.cgi
require_cfg XYMONSERVER_CFG xymond/etcfiles/xymonserver.cfg

work=$(mktempdir); register_cleanup "rm -rf '$work'"

# post FORM : datepage.cgi's answer to the form, as the web server hands it over.
post() {
	printf '%s' "$1" | REQUEST_METHOD=POST CONTENT_TYPE=application/x-www-form-urlencoded \
		CONTENT_LENGTH=${#1} XYMONHOME="$work" \
		"$DATEPAGE" --env="$XYMONSERVER_CFG" --url=/mon/periodic 2>"$work/stderr" || :
}

for form in 'TYPE=day&DAY=25&MONTH=10&YEAR=2026|/mon/periodic/daily/2026/10/25/' \
            'TYPE=week&WEEK=43&YEAR=2026|/mon/periodic/weekly/2026/43/' \
            'TYPE=month&MONTH=10&YEAR=2026|/mon/periodic/monthly/2026/10/'; do
	query=${form%|*} want=${form#*|}
	assert_contains "URL=$want\"" "$(post "$query")" \
		"datepage.cgi did not send '$query' to $want"
done

# No TYPE: nothing to build, so an empty URL rather than whatever the buffer held.
assert_contains 'URL="' "$(post 'DAY=25&MONTH=10&YEAR=2026')" \
	"datepage.cgi printed something other than an empty URL for a form without TYPE"

pass "datepage.cgi sends a day, a week and a month to the report under its --url"
