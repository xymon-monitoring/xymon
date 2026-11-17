#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/web/appfeed-detail.sh
#
# appfeed.cgi with host= and test= answers with that one status and its full
# text, for a phone app that cannot afford the status web page. With a real
# xymond holding web01.example.com and web01-example.com, checked:
#
#   - without host=/test=, the feed is as before: every non-green status, no
#     <Message>;
#   - with them, exactly that status: the decoy web01-example.com, which the
#     name would match if its dots were "any character", is not in it;
#   - its <Message> holds every line of the text, a "|" included, and stays
#     well-formed XML when the text holds "]]>" or a control character;
#   - a status that is green now is still answered, with its colour;
#   - a host name with a space (which would add a filter to the query) is
#     refused;
#   - text from the monitored host or an operator cannot add elements: a
#     first line or a disable reason that closes its CDATA and opens an
#     element of its own stays text, in the plain feed too.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
# shellcheck source=tests/lib/xymond-daemon.sh
. "$(dirname "$0")/../lib/xymond-daemon.sh"

ROOT=$(find_root)
require_c_buildenv "$ROOT"
require_bin XYMOND xymond/xymond
require_bin XYMONCLIENT common/xymon
require_shm_segments "$(grep -c 'setup_channel(C_[A-Z_]*, CHAN_MASTER)' "$ROOT/xymond/xymond.c")"

pcre_libs=${PCRELIBS:-$(xymon_makevar "$ROOT" PCRELIBS)}
[ -n "$pcre_libs" ] || pcre_libs="-lpcre2-8"

work=$(mktempdir)
build_xymon_libs "$ROOT" "$work/libbuild.log" libxymon.a libxymoncomm.a
cflags=$(xymon_cflags "$ROOT")
ldflags=$(xymon_ldflags "$ROOT")
# shellcheck disable=SC2086  # the flag lists are meant to split
"$CC" $cflags -o "$work/appfeed" "$ROOT/web/appfeed.c" \
	"$(xymon_lib "$ROOT" libxymon.a)" "$(xymon_lib "$ROOT" libxymoncomm.a)" "$(xymon_lib "$ROOT" libxymon.a)" \
	$pcre_libs $ldflags 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; fail "appfeed does not compile"; }

mkdir -p "$work/etc" "$work/home/etc" "$work/home/tmp"
{
	printf '127.0.0.1 web01-example.com # \n'
	printf '127.0.0.1 web01.example.com # \n'
} >"$work/etc/hosts.cfg"
require_cfg XYMONSERVER_CFG xymond/etcfiles/xymonserver.cfg
sed -e 's|^XYMONHOME=.*|XYMONHOME="'"$work"'/home"|' \
    -e 's|^XYMONTMP=.*|XYMONTMP="'"$work"'/home/tmp"|' \
	"$XYMONSERVER_CFG" > "$work/xymond.cfg"

XYMOND_PID=""
stop_xymond() {
	[ -n "$XYMOND_PID" ] || return 0
	kill "$XYMOND_PID" 2>/dev/null || true
	wait "$XYMOND_PID" 2>/dev/null || true
	XYMOND_PID=""
}
register_cleanup stop_xymond

xymond_launch() {
	local port=$1; shift
	"$XYMOND" --no-daemon --listen="127.0.0.1:$port" \
		--hosts="$work/etc/hosts.cfg" --env="$work/xymond.cfg" \
		--pidfile="$work/xymond.pid" --checkpoint-file="$work/chk.out" \
		"$@" \
		> "$work/xymond.log" 2>&1 &
	XYMOND_PID=$!
}

start_xymond
printf 'XYMONDPORT="%s"\nXYMSRV="127.0.0.1"\nXYMONSERVERCGIURL="/xymon-cgi"\nHTMLCONTENTTYPE="text/html"\n' \
	"$PORT" > "$work/cgi.cfg"

x() { XYMON_TIMEOUT=5 "$XYMONCLIENT" "127.0.0.1:$PORT" "$1"; }

x "status web01-example,com.conn red the other host" >/dev/null
x "$(printf 'status web01,example,com.conn red Host not responding\nline two | with a pipe\nend ]]> here, and a \001 byte\nlast line')" >/dev/null
x "status web01,example,com.cpu green load is fine" >/dev/null
inject=']]></MessageSummary><Injected/><MessageSummary><![CDATA['
x "$(printf 'status web01-example,com.disk red %s \001\nsecond line' "$inject")" >/dev/null
x "status web01-example,com.procs red too few processes" >/dev/null
x "disable web01-example,com.procs 60 $inject" >/dev/null
for _ in $(seq 1 50); do
	got=$(x 'xymondboard host=^web01.example.com$ test=^cpu$ fields=color')
	case $got in *green*) break ;; esac
	sleep 0.1
done

# feed QUERY -- appfeed.cgi's answer, in OUT, and its exit status, in RC
feed() {
	set +e
	OUT=$(env REQUEST_METHOD=GET QUERY_STRING="$1" "$work/appfeed" --env="$work/cgi.cfg" 2>&1)
	RC=$?
	set -e
}

feed ""
[ "$RC" -eq 0 ] || fail "the plain feed failed (exit $RC)"
assert_contains "<Servername>web01-example.com</Servername>" "$OUT" "the plain feed lost a non-green status"
assert_not_contains "<Message>" "$OUT" "the plain feed now carries full messages"
assert_not_contains "<Type>cpu</Type>" "$OUT" "the plain feed lists a green status"
# The injected text, split out of the CDATA rather than ending it
case $OUT in *'<MessageSummary>'*'<Injected/>'*) ;; *) fail "the injection test status is not in the plain feed" ;; esac
split=']]]]><![CDATA[></MessageSummary><Injected/>'
n=$(printf '%s\n' "$OUT" | grep -c 'Injected' || true)
m=$(printf '%s\n' "$OUT" | grep -F -c "$split" || true)
[ "$n" -eq "$m" ] || fail "a first line or disable reason added an element to the feed: $(printf '%s\n' "$OUT" | grep Injected)"
case $OUT in *$'\001'*) fail "a control character in a first line reached the XML" ;; esac

# A disabled status is blue, so not in the list: ask for it by name
for _ in $(seq 1 50); do
	feed "host=web01-example.com&test=procs"
	case $OUT in *'<DisableText>'*) break ;; esac
	sleep 0.1
done
assert_contains "<DisableText><![CDATA[$split" "$OUT" "a disable reason was not split out of its CDATA"
n=$(printf '%s\n' "$OUT" | grep -c 'Injected' || true)
m=$(printf '%s\n' "$OUT" | grep -F -c "$split" || true)
[ "$n" -eq "$m" ] || fail "a disable reason added an element: $(printf '%s\n' "$OUT" | grep Injected)"

feed "host=web01.example.com&test=conn"
[ "$RC" -eq 0 ] || fail "the single-status request failed (exit $RC)"
assert_contains "<Servername>web01.example.com</Servername>" "$OUT" "the single status is not in the answer"
assert_not_contains "web01-example.com" "$OUT" "the answer holds a host the name only resembles"
assert_contains "line two | with a pipe" "$OUT" "the full text is missing a line, or its |"
assert_contains "last line" "$OUT" "the full text is cut short"
assert_contains "end ]]]]><![CDATA[> here" "$OUT" "a ]]> in the text was not split out of the CDATA"
case $OUT in *$'\001'*) fail "a control character reached the XML" ;; esac
n=$(printf '%s\n' "$OUT" | grep -c '<ServerStatus>' || true)
[ "$n" -eq 1 ] || fail "the single-status answer holds $n statuses"

feed "host=web01.example.com&test=cpu"
assert_contains "<Status>green</Status>" "$OUT" "a status that is green now is not answered"
assert_contains "load is fine" "$OUT" "the green status's text is missing"

feed "host=web01%20color%3Dred&test=conn"
[ "$RC" -ne 0 ] || fail "a host name with a space was accepted"
assert_not_contains "<ServerStatus>" "$OUT" "a host name with a space returned statuses"

pass "appfeed.cgi answers host= and test= with that one status and its full text, as well-formed XML"
