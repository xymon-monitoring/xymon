#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/xymond/locator-extras.sh
#
# xymond_locator must return a server's extras whole.
#
# A worker registers with --locatorextra, which the web CGIs use as the URL of
# that server's CGI directory, and an extended query ("X") returns it after
# the server name. handle_request() appended it with
# snprintf(buf+blen, sizeof(buf)-blen-1, ...), but buf is a pointer there:
# sizeof(buf) is the size of a pointer, the subtraction wrapped around to a
# huge size_t, and what snprintf() did with that depended on the C library.
# On glibc 2.34 the extras came back whole; on glibc 2.39 they lost their last
# character, so every URL built from them pointed somewhere that does not
# exist. handle_request() now gets the real buffer size.
#
# Runs the real daemon and asks it with lib/locator, the interactive client
# built beside it.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin XYMOND_LOCATOR xymond/xymond_locator
require_bin LOCATOR lib/locator

work=$(mktempdir)
export XYMONTMP="$work"

# answers PORT -- the locator on PORT replies to a ping.
answers() {
	grep -q "Locator is available" <<<"$("$LOCATOR" "127.0.0.1:$1" </dev/null 2>/dev/null)"
}

# Start the locator on a free UDP port; a taken one makes it exit at once,
# which is a retry on another port.
pid= PORT=
for try in 1 2 3 4 5 6 7 8 9 10; do
	p=$(( 20000 + (RANDOM % 20000) ))
	"$XYMOND_LOCATOR" --listen="127.0.0.1:$p" --logfile="$work/locator.log" &
	cand=$!
	up=
	for _ in {1..20}; do
		kill -0 "$cand" 2>/dev/null || break
		if answers "$p"; then up=1; break; fi
		sleep 0.1
	done
	if [ -n "$up" ]; then pid=$cand; PORT=$p; break; fi
	kill "$cand" 2>/dev/null || true
	wait "$cand" 2>/dev/null || true
done
[ -n "$pid" ] || fail "xymond_locator did not come up: $(cat "$work/locator.log" 2>/dev/null)"
register_cleanup "kill $pid 2>/dev/null || true"

extra="http://worker.example/xymon-cgi"
printf '%s\n' \
	"r s 10.0.0.9:1988 hostdata 2 1 $extra" \
	"r h 10.0.0.9:1988 hostdata realhost" \
	"x realhost hostdata" \
	| "$LOCATOR" "127.0.0.1:$PORT" > "$work/query.log" 2>&1

assert_contains "Result: 10.0.0.9:1988" "$(cat "$work/query.log")" \
	"the extended query did not name the registered server"
got=$(sed -n 's/^ *Extras gave: //p' "$work/query.log")
assert_equal "$extra" "$got" "the locator did not return the server's extras whole"

pass "xymond_locator returns a server's extras whole"
