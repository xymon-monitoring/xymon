#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/web/svcstatus-histlog-traversal.sh
#
# Regression guard for the CGI path-traversal series #145 / #146 / #147:
# the historical-log path ("$XYMONHISTLOGS/<host>/<service>/<tstamp>").
#
# A collapsing "service" (from SERVICE, HOSTSVC or SECTION) must be refused
# before the path is built.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)

require_c_buildenv "$ROOT"
# shellcheck source=tests/lib/svcstatus-cgi.sh
. "$(dirname "$0")/../lib/svcstatus-cgi.sh"

svcstatus_setup
svcstatus_build || { cat "$work/cc.log" >&2; fail "svcstatus does not build -- cannot verify the traversal guards"; }

# Historical-log canaries: a stray file in the host dir (reachable via
# SERVICE=".") and one above it (SERVICE=".."), in $XYMONHISTLOGS/<host>/...
mkdir -p "$work/var/histlogs/realhost/cpu"
printf 'CANARY-HISTLOG-HOSTDIR\n' >"$work/var/histlogs/realhost/CANARY_HISTDIR"
printf 'CANARY-HISTLOG-ABOVE\n'   >"$work/var/histlogs/CANARY_HISTUP"

# The guard fires before the host lookup, so a refusal returns its own
# "Status: 403"; a leak would instead reach loadhostdata() ("Status: 500",
# no xymond here) or disclose the canary. Assert both, so the check has
# teeth against either failure.
assert_histlog_refused() {  # <query-string> <what>
	render "$1"
	case "$OUT" in
		*CANARY-HISTLOG-HOSTDIR*|*CANARY-HISTLOG-ABOVE*)
			fail "$2: '$1' disclosed a histlog canary (#147 review)" ;;
	esac
	assert_contains "Status: 403" "$OUT" \
		"$2: '$1' was not refused at the traversal guard (reached host load or was served)"
}

for svc in "." ".." "a/b" "../.." "cpu/.."; do
	for target in CANARY_HISTDIR CANARY_HISTUP; do
		assert_histlog_refused "HOST=realhost&SERVICE=$svc&TIMEBUF=$target" \
			"histlog SERVICE traversal"
	done
done

# A control byte in SERVICE (charset-free on this path) would otherwise
# survive into the "test=<svc>" xymond request as a second protocol line, so
# it must be refused at the confinement guard (Status: 403), not
# confined-and-served (which would reach the host load / 404 instead).
for bad in "cpu%0Aget config" "cpu%09x" "cpu%7f"; do
	render "HOST=realhost&SERVICE=$bad&TIMEBUF=20260101"
	assert_contains "Status: 403" "$OUT" \
		"SERVICE control byte '$bad' was not refused -- protocol injection (#147 review follow-up)"
done

# SECTION reaches the same "service" variable, so a collapsing SECTION must be
# refused on the historical path too.
for sec in "." ".."; do
	assert_histlog_refused "HOST=realhost&SECTION=$sec&TIMEBUF=CANARY_HISTUP" \
		"histlog SECTION traversal"
done

# A SECTION with a traversal-shaped prefix: the service copy is the section's
# final path component (as basename() historically produced, so access-control
# groups keyed on it keep working), so the prefix cannot escape -- the lookup
# stays confined under the host's own directory and must disclose nothing.
render "HOST=realhost&SECTION=../CANARY_HISTUP&TIMEBUF=CANARY_HISTUP"
case "$OUT" in
	*CANARY-HISTLOG-HOSTDIR*|*CANARY-HISTLOG-ABOVE*)
		fail "histlog SECTION traversal: '../CANARY_HISTUP' disclosed a histlog canary (#147 review)" ;;
esac
[ "$RC" -eq 1 ] || fail "histlog SECTION '../CANARY_HISTUP' was served (exit $RC), not refused"

echo "OK $(basename "$0")"
