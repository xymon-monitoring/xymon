#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/web/svcstatus-section-filter.sh
#
# Regression guard for #147 review: SECTION as a live client-log filter.
#
# On a live CLIENT request, SECTION is a filter forwarded to xymond as
# "clientlog <host> section=<value>"; section names legitimately contain '/'
# (e.g. "msgs:/var/log/messages"). Emptying it would drop the filter and dump
# the whole client data. With no xymond here, svcstatus echoes the request it
# built -- which must still carry the full section filter.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)

require_c_buildenv "$ROOT"
# shellcheck source=tests/lib/svcstatus-cgi.sh
. "$(dirname "$0")/../lib/svcstatus-cgi.sh"

svcstatus_setup
svcstatus_build || { cat "$work/cc.log" >&2; fail "svcstatus does not build -- cannot verify the SECTION handling"; }

# SERVICE is compiled as a PCRE testname pattern for the aggregate/multi-test
# view, so a pattern with regex metacharacters must pass the confinement guard
# and reach do_request. The parse guard refuses with "Status: 403"; a value
# that passes it instead reaches loadhostdata(), which here fails against the
# dead xymond with "Status: 500 / Cannot load host configuration". (Both error
# pages share the title "Invalid request", so key on the status, not the body.)
render_live "HOST=realhost&SERVICE=cpu|disk"
assert_not_contains "Status: 403" "$OUT" \
	"aggregate SERVICE 'cpu|disk' was refused at the confinement guard -- regex view unreachable (#147 review round 5)"
assert_contains "Cannot load host configuration" "$OUT" \
	"aggregate SERVICE 'cpu|disk' did not reach do_request/loadhostdata past the guard (#147 review round 5)"

render_live "CLIENT=realhost&SECTION=msgs:/var/log/messages"
assert_contains "section=msgs:/var/log/messages" "$OUT" \
	"SECTION with '/' was dropped -- would disclose the full client dump (#147 review)"

# A SECTION ending in '/' (a directory-monitoring section) has an empty final
# path component; basename() historically served it, so the request must not
# 403 at the confinement guard. It reaches the clientlog request (which then
# fails only because no xymond is listening), still carrying the full filter --
# had it been refused, the request would never be built and no "section=" would
# appear.
render_live "CLIENT=realhost&SECTION=dir:/var/log/"
assert_contains "section=dir:/var/log/" "$OUT" \
	"SECTION ending in '/' was refused at the guard -- basename served it before (#147 review follow-up)"

# A client section leaf is free-form (client section names carry spaces,
# brackets, etc.), so it is confined against traversal but NOT to CGI_NAMECHARS
# -- a space in the leaf must still forward the filter, not 403 the request.
# (the echoed request htmlquotes the space as &nbsp;; a 403 would carry no
# "section=" at all.)
render_live "CLIENT=realhost&SECTION=Disk Usage"
assert_contains "section=Disk&nbsp;Usage" "$OUT" \
	"space-bearing SECTION leaf was refused -- client sections must stay viewable (#147 review round 4)"

# A SECTION whose leaf merely happens to equal CLIENTCOLUMN ("clientlog") is a
# request for that one section, not the whole client log: the filter must
# survive, not be dropped by the CLIENTCOLUMN shortcut.
render_live "CLIENT=realhost&SECTION=msgs:/var/log/clientlog"
assert_contains "section=msgs:/var/log/clientlog" "$OUT" \
	"SECTION leaf 'clientlog' dropped the filter and dumped the whole log (#147 review round 4)"

# A control byte in SECTION reaches both the "section=" filter and the request
# built from it, so it must be refused at the guard (Status: 403), not
# forwarded as an injected protocol line. Test BOTH parameter orders: SECTION
# is resolved after the whole query is parsed, so an invalid SECTION must
# refuse whether CLIENT (which also writes "service") comes before or after it
# -- otherwise a trailing CLIENT would overwrite the refusal and dump the log.
render_live "CLIENT=realhost&SECTION=msgs%0Aget hostinfo"
assert_contains "Status: 403" "$OUT" \
	"SECTION with an embedded newline was not refused -- protocol injection (#147 review follow-up)"
assert_not_contains "section=msgs" "$OUT" \
	"SECTION with an embedded newline still built a filter"

render_live "SECTION=%01&CLIENT=realhost"
assert_contains "Status: 403" "$OUT" \
	"invalid SECTION before CLIENT was not refused -- trailing CLIENT overwrote the refusal (#147 review follow-up)"
assert_not_contains "Req=clientlog" "$OUT" \
	"invalid SECTION before CLIENT still built a clientlog request"

# web_access_allowed() can grant view access via a group named by the service;
# for SECTION that service is the section's final path component ("messages"
# for "msgs:/var/log/messages", as basename() historically produced). An empty
# service there would make that grant impossible and 403 every path-bearing
# SECTION on an access-controlled site (#147 review).
printf 'messages: alice\n' >"$work/etc/access.cfg"
RENDER_ENV="REMOTE_USER=alice" \
	render_live "CLIENT=realhost&SECTION=msgs:/var/log/messages" --access="$work/etc/access.cfg"
assert_not_contains "restricted" "$OUT" \
	"section-derived service no longer reaches the access check -- grant by section basename broken (#147 review)"
assert_contains "section=msgs:/var/log/messages" "$OUT" \
	"access-granted SECTION request did not carry its filter"

# SECTION equal to CLIENTCOLUMN ("clientlog") is the old spelling for "the
# whole client log": the single-variable code dropped the filter there, so the
# request must go out with no section= filter (a literal section=clientlog
# filter matches nothing and turns the page into "<No data>").
render_live "CLIENT=realhost&SECTION=clientlog"
assert_contains "Req=clientlog&nbsp;realhost," "$OUT" \
	"SECTION=clientlog: expected the plain clientlog request echo (htmlquoted)"
assert_not_contains "section=" "$OUT" \
	"SECTION=clientlog was forwarded as a filter instead of dropped (#147 review)"

echo "OK $(basename "$0")"
