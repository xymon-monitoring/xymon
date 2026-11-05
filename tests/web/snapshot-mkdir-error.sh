#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/web/snapshot-mkdir-error.sh
#
# snapshot.cgi creates a directory per request under XYMONSNAPDIR. When that
# failed it answered "Cannot create output directory" and nothing else: no
# reason on the page, and nothing in the CGI error log, so an admin could not
# tell a missing directory from a permission problem, nor find which one.
#
# The reason now goes on the page, and the full path with the reason goes to
# the CGI's error log: stderr, which is the web server's error log, or
# cgierror.log when XYMONCGILOGDIR is set (redirect_cgilog()). The test sets
# XYMONCGILOGDIR so it can read what was logged.
# The path stays off the page, which anyone allowed to use the CGI can read.
#
# Checked with XYMONSNAPDIR under a directory that does not exist, which
# fails the same way for every user, root included.
#
# Needs a built tree: web/snapshot.cgi.
set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin SNAPSHOT web/snapshot.cgi

work=$(mktempdir); register_cleanup "rm -rf '$work'"
missing="$work/no-such-dir/snap"

page=$(REQUEST_METHOD=GET QUERY_STRING='mon=1&day=1&yr=2020&hour=0&min=0&sec=0' \
XYMONHOME="$work" XYMONSNAPDIR="$missing" XYMONCGILOGDIR="$work" \
HTMLCONTENTTYPE=text/html "$SNAPSHOT" 2>"$work/stderr" || :)
log=$(cat "$work/cgierror.log" 2>/dev/null || :)

assert_contains "No such file or directory" "$page" \
"the page does not say why the snapshot directory could not be created: $page"
assert_not_contains "$work" "$page" \
"the page shows a filesystem path to whoever made the request"
assert_contains "$missing/" "$log" \
"cgierror.log does not name the directory that could not be created: $log"
assert_contains "No such file or directory" "$log" \
"cgierror.log does not say why the snapshot directory could not be created: $log"

pass "snapshot.cgi says why it cannot create its directory, and logs which one, without showing the path on the page"
