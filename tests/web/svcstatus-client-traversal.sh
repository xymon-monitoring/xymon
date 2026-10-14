#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/web/svcstatus-client-traversal.sh
#
# Regression guard for the CGI path-traversal series #145 / #146 / #147:
# the CLIENT/TIMEBUF client-data path ("$CLIENTLOGS/<host>/<tstamp>").
#
# svcstatus.cgi builds filesystem paths from request-supplied components.
# This drives the real CGI against canary files and asserts that every
# non-component value is refused outright (exit 1, "Invalid request") --
# not merely that no canary leaks. "Refused" matters: a value like
# "../realhost" must NOT be silently served as "realhost".

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)

require_c_buildenv "$ROOT"
# shellcheck source=tests/lib/svcstatus-cgi.sh
. "$(dirname "$0")/../lib/svcstatus-cgi.sh"

svcstatus_setup
svcstatus_build || { cat "$work/cc.log" >&2; fail "svcstatus does not build -- cannot verify the traversal guards"; }

# Canaries above and inside CLIENTLOGS, plus a legitimate client-data file.
mkdir -p "$work/var/hostdata/realhost"
printf 'CANARY-ABOVE-ROOT\n'  >"$work/var/CANARY_ABOVE"
printf 'CANARY-IN-ROOT\n'     >"$work/var/hostdata/CANARY_IN"
printf 'legitimate client data\n' >"$work/var/hostdata/realhost/20260101"

# assert_refused <query-string> <what> -- the request must hit the
# errormsg() refusal, not be served (exit 0) after silent normalization.
assert_refused() {
	render "$1"
	[ "$RC" -eq 1 ] || fail "$2: QUERY_STRING='$1' was served (exit $RC), not refused"
	assert_contains "Invalid request" "$OUT" \
		"$2: QUERY_STRING='$1' refused without the expected error text"
}

# None of these name a real host: "." / "/" collapse onto the root, ".."
# escapes upward, and "," / ",," become "." / ".." after the CLIENT rewrite.
# Each must be refused outright, with no canary returned.
for host in ".." "." "/" "//" "../" "/.." "," ",," ",,/" "./" ",."; do
	for target in CANARY_ABOVE CANARY_IN; do
		render "CLIENT=$host&TIMEBUF=$target"
		case "$OUT" in
			*CANARY-ABOVE-ROOT*|*CANARY-IN-ROOT*)
				fail "path traversal: CLIENT='$host' disclosed $target (#145/#146/#147)"
				;;
		esac
		[ "$RC" -eq 1 ] || fail "CLIENT='$host' was served (exit $RC), not refused"
	done
done

# Slash-bearing values must be refused, not normalized: basename() would
# reduce "../realhost" to "realhost", which a canary check alone cannot catch
# (it returned real data with exit 0 under that broken variant).
for host in "../realhost" "/realhost" ",,/realhost" "realhost/"; do
	assert_refused "CLIENT=$host&TIMEBUF=20260101" "normalized host served"
done
assert_refused "CLIENT=realhost&TIMEBUF=dir/20260101" "normalized TIMEBUF served"

# A legitimate request must still be served -- the guards must not have
# turned into a blanket refusal.
render "CLIENT=realhost&TIMEBUF=20260101"
assert_contains "legitimate client data" "$OUT" \
	"legitimate client-data request no longer served"

# Hosts named by IP take a different parse path (CLIENT rewrites ',' to '.'),
# so cover both spellings.
mkdir -p "$work/var/hostdata/192.168.1.1"
printf 'ipv4 client data\n' >"$work/var/hostdata/192.168.1.1/20260101"
for spelling in "192,168,1,1" "192.168.1.1"; do
	render "CLIENT=$spelling&TIMEBUF=20260101"
	assert_contains "ipv4 client data" "$OUT" \
		"IP-named host ($spelling) no longer served"
done

pass "the CLIENT/TIMEBUF client-data path refuses traversal (#145, #146, #147)"
