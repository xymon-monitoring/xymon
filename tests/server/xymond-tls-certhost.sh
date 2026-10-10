#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/xymond-tls-certhost.sh
#
# Where a host may send from its own hosts.cfg address, it may also send over
# TLS with a verified client certificate that names it.
#
# web01.example.com and db01.example.com are listed at addresses the test
# does not send from, and --status-senders and --maint-senders name neither
# that address nor 127.0.0.1, where the client is. With XYMOND_TLS_CA set,
# xymond asks TLS clients for a certificate, and web01.pem names
# web01.example.com (tests/fixtures/tls). Checked:
#
#   - with web01's certificate: web01's first status, a combo status, a
#     query, a client message, a data message, a modify and a disable are
#     accepted for web01, and a status for db01 is refused;
#   - the same messages over TLS without a certificate, or in plaintext, are
#     refused, as is a certificate the CA did not issue;
#   - without XYMOND_TLS_CA no certificate is asked for, so web01's is not
#     seen and its status is refused.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
# shellcheck source=tests/lib/xymond-daemon.sh
. "$(dirname "$0")/../lib/xymond-daemon.sh"

ROOT=$(find_root)
require_bin XYMOND xymond/xymond
require_bin XYMONCLIENT common/xymon
require_shm_segments "$(grep -c 'setup_channel(C_[A-Z_]*, CHAN_MASTER)' "$ROOT/xymond/xymond.c")"
case $(xymon_sslcflags "$ROOT") in *HAVE_OPENSSL*) ;; *) skip "this build has no OpenSSL: no TLS listener, no client certificates" ;; esac

work=$(mktempdir)
mkdir -p "$work/home/etc" "$work/home/tmp" "$work/home/www"
printf 'page test Test\n192.0.2.10 web01.example.com # conn\n192.0.2.11 db01.example.com # conn\n' > "$work/hosts.cfg"
cp "$ROOT"/tests/fixtures/tls/*.pem "$ROOT"/tests/fixtures/tls/*.key "$work/"
chmod 600 "$work"/*.key

P=$(free_port)
T=$(free_port)
require_cfg XYMONSERVER_CFG xymond/etcfiles/xymonserver.cfg
# cfg [CA] -- xymonserver.cfg, with XYMOND_TLS_CA set to CA when given
cfg() {
	sed -e 's|^XYMONHOME=.*|XYMONHOME="'"$work"'/home"|' \
	    -e 's|^XYMONTMP=.*|XYMONTMP="'"$work"'/home/tmp"|' \
		"$XYMONSERVER_CFG" > "$work/xymonserver.cfg"
	printf 'XYMOND_TLS_CERT="%s"\nXYMOND_TLS_KEY="%s"\nXYMOND_TLS_CA="%s"\n' \
		"$work/server.pem" "$work/server.key" "${1:-}" >> "$work/xymonserver.cfg"
}

MLPID=
launch() {
	"$XYMOND" --no-daemon --listen="127.0.0.1:$P" --tls-listen="127.0.0.1:$T" \
		--status-senders=192.0.2.1 --maint-senders=192.0.2.1 \
		--hosts="$work/hosts.cfg" --env="$work/xymonserver.cfg" \
		--pidfile="$work/xymond.pid" --checkpoint-file="$work/chk.out" \
		> "$work/xymond.log" 2>&1 &
	MLPID=$!
	for _ in $(seq 1 100); do
		grep -q 'Setup complete' "$work/xymond.log" 2>/dev/null && return 0
		kill -0 "$MLPID" 2>/dev/null || { MLPID=; return 1; }
		sleep 0.1
	done
	return 1
}
stop() { [ -n "$MLPID" ] || return 0; kill "$MLPID" 2>/dev/null || true; wait "$MLPID" 2>/dev/null || true; MLPID=; }
register_cleanup 'stop'

# as CERT MESSAGE -- send over TLS with CERT's certificate (none: no
# certificate; plain: in plaintext), and print the reply
as() {
	local who=$1 m=$2
	case $who in
	plain)	XYMON_TIMEOUT=5 "$XYMONCLIENT" "127.0.0.1:$P" "$m" 2>&1 || true ;;
	none)	env XYMON_TIMEOUT=5 XYMON_TLS_CA="$work/ca.pem" "$XYMONCLIENT" "xymons://127.0.0.1:$T" "$m" 2>&1 || true ;;
	*)	env XYMON_TIMEOUT=5 XYMON_TLS_CA="$work/ca.pem" XYMON_TLS_CERT="$work/$who.pem" XYMON_TLS_KEY="$work/$who.key" \
			"$XYMONCLIENT" "xymons://127.0.0.1:$T" "$m" 2>&1 || true ;;
	esac
}
# kept HOST.TEST -- the status xymond holds for it (asked in plaintext: no
# --www-senders restricts xymondlog)
kept() { sleep 0.5; as plain "xymondlog $1"; }

cfg "$work/ca.pem"
launch || fail "xymond did not start with XYMOND_TLS_CA: $(cat "$work/xymond.log")"

as web01 "status web01.example.com.cpu green from web01's certificate" >/dev/null
assert_contains "from web01's certificate" "$(kept web01.example.com.cpu)" \
	"web01's first status, sent with its certificate, was refused: $(grep Refused "$work/xymond.log" || true)"
as web01 "combo
status web01.example.com.disk green combo from web01's certificate
" >/dev/null
assert_contains "combo from web01's certificate" "$(kept web01.example.com.disk)" "a combo status for web01 with its certificate was refused"
assert_contains "from web01's certificate" "$(as web01 "query web01.example.com.cpu")" "a query about web01 with its certificate was refused"

as web01 "status db01.example.com.cpu red db01 from web01's certificate" >/dev/null
assert_not_contains "db01 from web01's certificate" "$(kept db01.example.com.cpu)" "web01's certificate was accepted for db01"
grep -q 'Refused message from 127.0.0.1: status db01.example.com.cpu' "$work/xymond.log" \
	|| fail "the status for db01 was not refused with its sender: $(cat "$work/xymond.log")"

for who in none plain rogue; do
	as "$who" "status web01.example.com.mem green web01 as $who" >/dev/null
	assert_not_contains "web01 as $who" "$(kept web01.example.com.mem)" "a status for web01 was kept without its certificate ($who)"
done
assert_not_contains "from web01's certificate" "$(as none "query web01.example.com.cpu")" "a query about web01 without a certificate was answered"

# A client message: refused from the address, accepted with the certificate
as none "client web01.example.com.linux linux
[date]
no certificate" >/dev/null
sleep 0.3
grep -q 'Invalid client message - sender 127.0.0.1 not allowed for host web01.example.com' "$work/xymond.log" \
	|| fail "a client message for web01 without its certificate was not refused: $(tail -5 "$work/xymond.log")"
n=$(grep -c 'Invalid client message' "$work/xymond.log")
as web01 "client web01.example.com.linux linux
[date]
with the certificate" >/dev/null
sleep 0.3
[ "$(grep -c 'Invalid client message' "$work/xymond.log")" = "$n" ] \
	|| fail "a client message for web01 with its certificate was refused: $(tail -3 "$work/xymond.log")"

# A data message, checked the same way
as none "data web01.example.com.trends
[cpu.rrd]
no certificate" >/dev/null
sleep 0.3
grep -q 'Invalid data message - sender 127.0.0.1 not allowed for host web01.example.com' "$work/xymond.log" \
	|| fail "a data message for web01 without its certificate was not refused: $(tail -5 "$work/xymond.log")"
n=$(grep -c 'Invalid data message' "$work/xymond.log")
as web01 "data web01.example.com.trends
[cpu.rrd]
with the certificate" >/dev/null
sleep 0.3
[ "$(grep -c 'Invalid data message' "$work/xymond.log")" = "$n" ] \
	|| fail "a data message for web01 with its certificate was refused: $(tail -3 "$work/xymond.log")"

# A modify, which adds its cause to the status it changes
as none "modify web01.example.com.disk red testsrc modified without the certificate" >/dev/null
as web01 "modify web01.example.com.disk red testsrc modified with web01's certificate" >/dev/null
got=$(kept web01.example.com.disk)
assert_not_contains "modified without the certificate" "$got" "a modify of web01 without its certificate was accepted"
assert_contains "modified with web01's certificate" "$got" "a modify of web01 with its certificate was refused"

# A disable, which --maint-senders guards
as none "disable web01.example.com.cpu 60 without the certificate" >/dev/null
assert_not_contains "without the certificate" "$(kept web01.example.com.cpu)" "a disable of web01 without its certificate was accepted"
as web01 "disable web01.example.com.cpu 60 with web01's certificate" >/dev/null
assert_contains "with web01's certificate" "$(kept web01.example.com.cpu)" "a disable of web01 with its certificate was refused"
stop

# Without XYMOND_TLS_CA, no certificate is asked for: web01's is never seen
cfg
launch || fail "xymond did not start without XYMOND_TLS_CA: $(cat "$work/xymond.log")"
as web01 "status web01.example.com.cpu green no CA configured" >/dev/null
assert_not_contains "no CA configured" "$(kept web01.example.com.cpu)" "a client certificate counted although XYMOND_TLS_CA is not set"
stop

pass "a verified client certificate naming a host lets it send for that host wherever its own address would, and for no other"
