#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/web/eventlog-top-links.sh
#
# The "Top changes" page of eventlog.cgi links each host to the event log of
# that host, with HOSTMATCH=^NAME$, and each test the same way with TESTMATCH.
# The name went into the pattern as it was, so its '.' matched any character
# (#797), and into the URL as it was, unencoded.
#
# Two hosts, web01.example.com and web01Xexample.com, each with a change. The
# link of web01.example.com is taken from the page and followed: the event
# log it opens must list web01.example.com and not web01Xexample.com. Every
# link must also be a valid URL, its pattern percent-encoded. And three tests,
# conn, my+te$t (pattern characters) and tést (a byte above 0x7f): each
# test's link, followed, must list the events of that test alone. The links
# also carry the viewer's own criteria: on a page filtered by HOSTMATCH, the
# host's link must still open that host alone, and a criterion holding a
# quote and a tag must come back encoded, not as markup.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin EVENTLOG web/eventlog.cgi

work=$(mktempdir)
mkdir -p "$work/hist" "$work/web"
printf '127.0.0.1 web01.example.com # \n127.0.0.1 web01Xexample.com # \n' >"$work/hosts.cfg"
t=$(( $(date +%s) - 600 ))
{
	printf 'web01.example.com conn %d %d 60 red green 1\n' "$t" "$((t - 60))"
	printf 'web01Xexample.com conn %d %d 60 red green 1\n' "$((t + 1))" "$((t - 60))"
	printf 'web01.example.com my+te$t %d %d 60 red green 1\n' "$((t + 2))" "$((t - 60))"
	printf 'web01.example.com t\303\251st %d %d 60 red green 1\n' "$((t + 3))" "$((t - 60))"
} >"$work/hist/allevents"
printf 'XYMONHOME="%s"\nXYMONHISTDIR="%s/hist"\nHOSTSCFG="%s/hosts.cfg"\nHTMLCONTENTTYPE="text/html"\n' \
	"$work" "$work" "$work" >"$work/env"

# eventlog QUERY ARGS... -- the page eventlog.cgi serves for a GET of QUERY
eventlog() {
	local query=$1
	shift
	REQUEST_METHOD=GET QUERY_STRING="$query" "$EVENTLOG" --env="$work/env" "$@" 2>/dev/null
}

# links QUERY -- the links of the Top changes page for QUERY, one per line.
# Bytes, not characters (LC_ALL=C): an unencoded pattern holds bytes that are
# not UTF-8, which a UTF-8 grep would drop, and the link with them.
links() {
	top=$(eventlog "$1" --top)
	LC_ALL=C grep -o 'href="eventlog.sh?[^"]*"' <<<"$top" || true
}
# hostlink LINKS HOST-PATTERN -- the link of the host the encoded name matches
hostlink() { awk -v h="$2" '/&amp;HOSTMATCH=[^&"]*"$/ && $0 ~ h && !n++' <<<"$1"; }
# follow LINK -- the event log the link opens
follow() { eventlog "$(sed -e 's/^href="eventlog.sh?//' -e 's/"$//' -e 's/&amp;/\&/g' <<<"$1")"; }

links=$(links "COUNTTYPE=events")
# Two host links and an "Other hosts" one, three test links and an "Other services" one
[ "$(grep -c . <<<"$links")" = 7 ] || fail "the Top changes page does not have its 7 links: $links"

# Only characters a URL may carry as they are; the pattern's ^ \ | $ encoded
bad=$(LC_ALL=C grep '[^A-Za-z0-9"=?&;%._/:-]' <<<"$links" || true)
[ -z "$bad" ] || fail "a Top changes link carries its pattern unencoded: $bad"

link=$(hostlink "$links" 'web01%5c%2eexample')
[ -n "$link" ] || fail "the Top changes page has no link for web01.example.com: $links"
log=$(follow "$link")
assert_contains "web01.example.com" "$log" "the link of web01.example.com opens no event of that host"
assert_not_contains "web01Xexample.com" "$log" "the link of web01.example.com opens the events of web01Xexample.com: the '.' matched any character"

# A page the viewer filtered (HOSTMATCH=web01): the host's link still opens
# that host alone, as its own HOSTMATCH comes last and the last one counts
link=$(hostlink "$(links "COUNTTYPE=events&HOSTMATCH=web01")" 'web01%5c%2eexample')
[ -n "$link" ] || fail "the filtered Top changes page has no link for web01.example.com"
assert_not_contains "web01Xexample.com" "$(follow "$link")" \
	"on a filtered page, the link of web01.example.com opens the events of web01Xexample.com"

# The viewer's criteria are carried on encoded: a quote cannot end the href
# (the page itself, as a link is cut at its first quote)
inj=$(eventlog 'COUNTTYPE=events&TESTMATCH=conn%22%3E%3Cb%3Ex|conn' --top)
assert_contains 'conn%22%3e%3cb%3ex%7cconn' "$inj" "the viewer's TESTMATCH is not carried on encoded"
assert_not_contains '"><b>' "$inj" "the viewer's TESTMATCH is carried into the links as it was, quote and tag included"

# Each test's link: the events of one test, of the three
tests=("conn" 'my+te$t' "$(printf 't\303\251st')")
testlinks=$(LC_ALL=C grep '&amp;TESTMATCH=[^&"]*"$' <<<"$links" || true)
[ "$(printf '%s\n' "$testlinks" | grep -c .)" = 3 ] || fail "the Top changes page has not one link per test: $testlinks"
while IFS= read -r tlink; do
	log=$(follow "$tlink" </dev/null)
	shown=
	for test in "${tests[@]}"; do
		if grep -qF -- "$test" <<<"$log"; then shown="$shown $test"; fi
	done
	case $shown in
	" "*" "*) fail "the test link $tlink opens the events of several tests:$shown" ;;
	"") fail "the test link $tlink opens the events of no test" ;;
	esac
done <<<"$testlinks"

pass "eventlog.cgi's Top changes links match each name literally and are encoded"
