#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/web/svcstatus-histlog-serving.sh
#
# The serving side of the historical-log path: the traversal guards
# (svcstatus-*-traversal.sh) must not refuse a legitimate legal-charset
# name. Pins:
#   - a dotted column name ("web.grp") stays reachable,
#   - a hostname with a literal ',' is looked up verbatim (HOST is not
#     comma-decoded; only CLIENT/HOSTSVC carry the 192,168,1,1 spelling),
#   - a histlog whose "Status unchanged in " line is longer than the
#     100-byte parse buffer renders instead of smashing the stack (the
#     off-by-one clamp; caught by the ASAN build when available).

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)

require_c_buildenv "$ROOT"
# shellcheck source=tests/lib/svcstatus-cgi.sh
. "$(dirname "$0")/../lib/svcstatus-cgi.sh"

# --no-daemon: this test stands up its own fake xymond below and points
# XYMONDPORT at it, so skip setup's dead-port probe and its XYMONDPORT line.
svcstatus_setup --no-daemon

# Prefer an ASAN build so the long-line case below detects a stack write
# past the parse buffer as a crash instead of silent corruption. An ASAN
# error must not exit 1 (render treats 1 as an ordinary refusal), so make
# it exit 99. Fall back to a plain build where ASAN is unavailable -- which
# asan_usable settles by running a probe, because a host can link the
# sanitizer and still not load it, and the CGI would then die at startup
# with exit 127, indistinguishable here from the crash we are hunting.
export ASAN_OPTIONS="exitcode=99${ASAN_OPTIONS:+:$ASAN_OPTIONS}"
{ asan_usable && svcstatus_build -g -O1 -fsanitize=address,undefined; } \
	|| svcstatus_build \
	|| { cat "$work/cc.log" >&2; fail "svcstatus does not build -- cannot verify histlog serving"; }

# The served page is rendered through the histlog templates.
mkdir -p "$work/web"
cp "$ROOT/xymond/webfiles/histlog_header" "$ROOT/xymond/webfiles/histlog_footer" "$work/web/"

# Serving a histlog needs a xymond answering "hostinfo clone=<host>"
# (lib/loadhosts_net.c): stand up the fake responder from tests/lib on an
# ephemeral port and point the CGI's XYMONDPORT at it. The reply is
# host-independent: the client keys the result on its own request, and
# DISPLAYNAME falls back to the requested hostname.
printf 'XMH_IP:127.0.0.1\n' >"$work/hostinfo.reply"
"$CC" -o "$work/fake-xymond" "$ROOT/tests/lib/fake-xymond.c" 2>"$work/cc-fake.log" \
	|| { cat "$work/cc-fake.log" >&2; fail "fake-xymond responder does not compile"; }
"$work/fake-xymond" "$work/hostinfo.reply" >"$work/fake-xymond.port" &
fakepid=$!
register_cleanup "kill $fakepid 2>/dev/null || true"
for _ in 1 2 3 4 5 6 7 8 9 10; do
	[ -s "$work/fake-xymond.port" ] && break
	sleep 0.2
done
[ -s "$work/fake-xymond.port" ] || fail "fake-xymond did not report its port"
echo "XYMONDPORT=\"$(cat "$work/fake-xymond.port")\"" >>"$work/etc/xymonserver.cfg"

echo '127.0.0.1 a,b #' >>"$work/etc/hosts.cfg"

# Histlog files live in "$XYMONHISTLOGS/<host with . and , as _>/<svc>/<tstamp>".
mkdir -p "$work/var/histlogs/realhost/cpu" \
	"$work/var/histlogs/realhost/web.grp" \
	"$work/var/histlogs/a_b/cpu"
printf 'green Fri Aug 7 12:00:00 2026 MARKER-DOTTED-SVC\nAll fine\n' \
	>"$work/var/histlogs/realhost/web.grp/20260101"
printf 'green Fri Aug 7 12:00:00 2026 MARKER-COMMA-HOST\nAll fine\n' \
	>"$work/var/histlogs/a_b/cpu/20260101"

assert_served() {  # <query-string> <marker> <what>
	render "$1"
	[ "$RC" -eq 0 ] || fail "$3: QUERY_STRING='$1' was refused (exit $RC), not served"
	assert_contains "$2" "$OUT" "$3: QUERY_STRING='$1' served without its log content"
}

assert_served "HOST=realhost&SERVICE=web.grp&TIMEBUF=20260101" \
	"MARKER-DOTTED-SVC" "dotted column name"
assert_served "HOST=a,b&SERVICE=cpu&TIMEBUF=20260101" \
	"MARKER-COMMA-HOST" "verbatim comma hostname"

# "Status unchanged in " parsing copies up to 100 bytes into a 100-byte
# stack buffer; a line of 150 chars ending at EOF (no trailing newline)
# exercised the off-by-one NUL write. render() flags any exit above 1 as a
# crash, which is what the ASAN build turns the overflow into.
{
	printf 'green Fri Aug 7 12:00:00 2026 MARKER-LONG-LINE\n'
	printf 'Status unchanged in '
	printf 'X%.0s' $(seq 1 150)
} >"$work/var/histlogs/realhost/cpu/20260101"
assert_served "HOST=realhost&SERVICE=cpu&TIMEBUF=20260101" \
	"MARKER-LONG-LINE" "oversized Status-unchanged line"

# A histlog file with no newline at all (e.g. truncated by a full disk) sets
# restofmsg to an empty string; the strstr() scans that follow must not
# dereference it. Must be >= 10 bytes (the stat size gate) and single-line.
mkdir -p "$work/var/histlogs/realhost/nonl"
printf 'green MARKER-NO-NEWLINE-nonewline-here' >"$work/var/histlogs/realhost/nonl/20260101"
assert_served "HOST=realhost&SERVICE=nonl&TIMEBUF=20260101" \
	"MARKER-NO-NEWLINE" "newline-less histlog file"

pass "the historical-log path serves a legitimate legal-charset name that the traversal guards must not refuse"
