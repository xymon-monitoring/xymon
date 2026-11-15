#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/web/svcstatus-critack-escape.sh
#
# A status page opened from the Critical Systems view shows the record's
# priority, resolver group and instructions, which come in the page's URL
# (NKPRIO, NKTTGROUP, NKTTEXTRA). The group and instructions were printed
# into the page as they came; they are now HTML-escaped. Checked, with the
# real svcstatus.cgi on a historical status:
#
#   - neither comes out as markup; each comes out escaped, its '"' as &quot;
#     and its '&' as &amp;;
#   - an ordinary group and instructions are shown as they are;
#   - with none in the URL, the cells stay blank (&nbsp;), as before.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
require_c_buildenv "$ROOT"
# shellcheck source=tests/lib/svcstatus-cgi.sh
. "$(dirname "$0")/../lib/svcstatus-cgi.sh"

# --no-daemon: a fake xymond below answers the host lookup a historical page makes
svcstatus_setup --no-daemon
svcstatus_build || { cat "$work/cc.log" >&2; fail "svcstatus does not build"; }

mkdir -p "$work/web"
cp "$ROOT/xymond/webfiles/histlog_header" "$ROOT/xymond/webfiles/histlog_footer" \
	"$ROOT/xymond/webfiles/critack_form" "$work/web/"

printf 'XMH_IP:127.0.0.1\n' >"$work/hostinfo.reply"
"$CC" -o "$work/fake-xymond" "$ROOT/tests/lib/fake-xymond.c" 2>"$work/cc-fake.log" \
	|| { cat "$work/cc-fake.log" >&2; fail "fake-xymond responder does not compile"; }
"$work/fake-xymond" "$work/hostinfo.reply" >"$work/fake-xymond.port" &
fakepid=$!
register_cleanup "stop_process $fakepid"
for _ in 1 2 3 4 5 6 7 8 9 10; do
	[ -s "$work/fake-xymond.port" ] && break
	sleep 0.2
done
[ -s "$work/fake-xymond.port" ] || fail "fake-xymond did not report its port"
echo "XYMONDPORT=\"$(cat "$work/fake-xymond.port")\"" >>"$work/etc/xymonserver.cfg"
# the templates are read from $XYMONHOME/web; the config would otherwise override it
echo "XYMONHOME=\"$work\"" >>"$work/etc/xymonserver.cfg"

mkdir -p "$work/var/histlogs/realhost/conn"
printf 'red Fri Aug 7 12:00:00 2026 conn NOT ok\nhost down\n' >"$work/var/histlogs/realhost/conn/20260101"

page="HOST=realhost&SERVICE=conn&TIMEBUF=20260101&NKPRIO=1"

# each value carries a '&', a '"' and markup
render "$page&NKTTGROUP=G%261%22%3E%3Cb%3EGROUP%3C%2Fb%3E&NKTTEXTRA=E%261%22%3E%3Cb%3EEXTRA%3C%2Fb%3E"
[ "$RC" -eq 0 ] || fail "the status page was refused (exit $RC)"
assert_contains "Resolver group" "$OUT" "the critical acknowledgement table is not on the page"
for f in GROUP EXTRA; do
	i=${f:0:1}
	assert_not_contains "<b>$f</b>" "$OUT" "the URL's $f value came out as markup"
	assert_contains "$i&amp;1&quot;&gt;&lt;b&gt;$f&lt;/b&gt;" "$OUT" "the URL's $f value is not shown escaped, & and \" included"
done

render "$page&NKTTGROUP=Unix%20team&NKTTEXTRA=Call%20the%20on-call"
assert_contains "<td align=center>Unix team</td>" "$OUT" "an ordinary group is not shown as it is"
assert_contains "Call the on-call" "$OUT" "ordinary instructions are not shown as they are"

# no group or instructions: the cells keep their &nbsp;, not an escaped "&amp;nbsp;"
render "$page"
assert_contains "<td align=center>&nbsp;</td>" "$OUT" "an empty group does not leave a blank cell"
assert_not_contains "&amp;nbsp;" "$OUT" "an empty group or instructions shows the text &nbsp;"

pass "the status page prints a critical system's group and instructions from its URL escaped"
