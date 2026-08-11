#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/web/history-reportlog-reject.sh
#
# history.cgi and reportlog.cgi share cgi_split_hostsvc()/cgi_pathcomponent()
# for their HISTFILE/HOSTSVC/HOST/SERVICE parameters (#147). Pins that a
# rejected or absent component is refused with the "Invalid request" page and
# never crashes -- the NULL-based rejection introduced two ways to die:
#   - history.c did displayname = strdup(hostname) with hostname==NULL for a
#     rejected HISTFILE (strdup(NULL) segfault), and
#   - its guard checked only !hostname/!service while the globals start as ""
#     so an absent or dotless HISTFILE sailed past into a bogus empty page.
# reportlog HOST also must refuse a path-shaped value ('realhost/..') whole,
# not truncate it to the servable 'realhost'.
#
# The CGIs are built under ASAN where available so a NULL deref is a crash
# (exit >= 2), not a silent read.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)

require_c_buildenv "$ROOT"
# shellcheck source=tests/lib/svcstatus-cgi.sh
. "$(dirname "$0")/../lib/svcstatus-cgi.sh"

# svcstatus_setup gives us $work, the scraped link libraries, and a minimal
# xymonserver.cfg with a configured "realhost"; the CGIs here only exercise
# the pre-daemon rejection path, so the dead-port endpoint is unused.
svcstatus_setup

# "Where available" is settled by running a probe, not by compiling one: a
# host can link the sanitizer and still fail to load it, and the CGI would
# then exit 127 at startup -- which run_cgi below reads as the very crash
# this test hunts for. Without it, build plain and keep the weaker check.
cflags="-g -O1 -fsanitize=address,undefined"
asan_usable || cflags=""
export ASAN_OPTIONS="exitcode=99${ASAN_OPTIONS:+:$ASAN_OPTIONS}"
for cgi in history reportlog; do
	"$CC" $cflags -I"$ROOT/include" -I"$ROOT/lib" -I"$ROOT/web" -o "$work/$cgi" \
		"$ROOT/web/$cgi.c" \
		"$ROOT/lib/libxymon.a" "$ROOT/lib/libxymoncomm.a" "$ROOT/lib/libxymon.a" \
		$pcre_libs $ssllibs $netlibs $librtdef 2>"$work/cc-$cgi.log" \
	|| { "$CC" -I"$ROOT/include" -I"$ROOT/lib" -I"$ROOT/web" -o "$work/$cgi" \
			"$ROOT/web/$cgi.c" \
			"$ROOT/lib/libxymon.a" "$ROOT/lib/libxymoncomm.a" "$ROOT/lib/libxymon.a" \
			$pcre_libs $ssllibs $netlibs $librtdef 2>"$work/cc-$cgi.log" \
		|| { cat "$work/cc-$cgi.log" >&2; fail "$cgi.cgi does not build"; }; }
done

# run_cgi <cgi> <query-string> -- sets OUT and RC; a clean errormsg() refusal
# is exit 1, so anything >= 2 (134 SIGABRT, 139 SIGSEGV, 99 ASAN) is a crash.
run_cgi() {
	set +e
	OUT=$(REQUEST_METHOD=GET QUERY_STRING="$2" \
	      XYMONHOME="$work" XYMONVAR="$work/var" \
	      ASAN_OPTIONS="detect_leaks=0:exitcode=99${ASAN_OPTIONS:+:$ASAN_OPTIONS}" \
		"$work/$1" --env="$work/etc/xymonserver.cfg" 2>/dev/null)
	RC=$?
	set -e
	[ "$RC" -le 1 ] || fail "$1.cgi exited $RC (crash?) on QUERY_STRING='$2'"
}

# assert_reject <cgi> <query-string> <what>
assert_reject() {
	run_cgi "$1" "$2"
	[ "$RC" -eq 1 ] || fail "$3: $1.cgi QUERY_STRING='$2' was not refused (exit $RC)"
	assert_contains "Invalid request" "$OUT" \
		"$3: $1.cgi QUERY_STRING='$2' refused without the expected page"
}

# history HISTFILE: traversal-shaped, dotless, and absent values all refuse
# without crashing (strdup(NULL) / empty-global guard bypass).
assert_reject history "HISTFILE=.."          "traversal HISTFILE"
assert_reject history "HISTFILE=."            "dot HISTFILE"
assert_reject history "HISTFILE=a/b.cpu"      "slash-bearing HISTFILE"
assert_reject history "HISTFILE=realhost"     "dotless HISTFILE (empty service)"
assert_reject history ""                      "absent HISTFILE (empty globals)"
# NOTE: the shell-metacharacter service half (HISTFILE=host.conn;id -> popen
# "tail ... %s" injection) is pinned at the unit level in
# cgi-split-hostsvc-harness.c (check("realhost.conn;id", "realhost", NULL)):
# here, against the dead xymond, a passed-guard request fails downstream with
# the same "Invalid request" page, so a CGI-level assert could not tell a
# parse-time refusal from a later failure.

# reportlog HOSTSVC/HOST/SERVICE: same rejection contract.
assert_reject reportlog "HOSTSVC=.."          "traversal HOSTSVC"
assert_reject reportlog ""                    "absent HOSTSVC"
assert_reject reportlog "HOST=realhost/..&SERVICE=cpu" "path-shaped HOST (must refuse whole, not truncate)"
assert_reject reportlog "HOST=realhost&SERVICE=cpu/.." "path-shaped SERVICE"
# An out-of-charset byte refuses the whole value; the old code truncated
# "cpu bar" to "cpu" and served that report (#147 review round 3).
assert_reject reportlog "HOST=realhost&SERVICE=cpu%20bar" "space-bearing SERVICE (refuse whole, not the 'cpu' report)"

echo "OK $(basename "$0")"
