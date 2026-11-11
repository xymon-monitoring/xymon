#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/xymond-transport.sh
#
# xymond records how each status arrived, next to its sender, and shows it
# in the xymondboard/xymondlog "transport" field. Checked:
#
#   - a status sent in plaintext reads "plaintext";
#   - one sent over TLS without a client certificate reads the TLS version
#     ("TLSv1.2" or "TLSv1.3"), with nothing after it;
#   - one sent with web01's certificate (tests/fixtures/tls), for web01,
#     reads the TLS version followed by " cert";
#   - the same certificate sending for db01, which it does not name, reads
#     the TLS version alone;
#   - a later plaintext report replaces a TLS one, so the field follows the
#     most recent report;
#   - a status relayed with a "Status message received from" line -- as
#     xymond_client relays the cpu, disk, ... statuses it derives from a
#     client message -- takes the transport of the host's last client
#     message (web01's, sent with its certificate), not the relay's own
#     connection; a host with no client message gets none;
#   - the client column ("clientlog") shows that client message's own
#     transport and sender, where it used to show neither; the info and
#     trends columns, which xymond makes up itself, still show none.
#
# No --*-senders list is set, so every message is accepted by address and
# only the transport differs.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
# shellcheck source=tests/lib/xymond-daemon.sh
. "$(dirname "$0")/../lib/xymond-daemon.sh"

ROOT=$(find_root)
require_bin XYMOND xymond/xymond
require_bin XYMONCLIENT common/xymon
require_shm_segments "$(grep -c 'setup_channel(C_[A-Z_]*, CHAN_MASTER)' "$ROOT/xymond/xymond.c")"
xymon_ssl_known "$ROOT" || skip "cannot tell whether this build has OpenSSL: no top-level Makefile, and XYMON_SSLCFLAGS is not set"
case $(xymon_sslcflags "$ROOT") in *HAVE_OPENSSL*) ;; *) skip "this build has no OpenSSL: no TLS listener" ;; esac

work=$(mktempdir)
mkdir -p "$work/home/etc" "$work/home/tmp" "$work/home/www"
printf 'page test Test\n127.0.0.1 web01.example.com # conn\n127.0.0.1 db01.example.com # conn\n' > "$work/hosts.cfg"
cp "$ROOT"/tests/fixtures/tls/*.pem "$ROOT"/tests/fixtures/tls/*.key "$work/"
chmod 600 "$work"/*.key

P=$(free_port)
T=$(free_port)
require_cfg XYMONSERVER_CFG xymond/etcfiles/xymonserver.cfg
sed -e 's|^XYMONHOME=.*|XYMONHOME="'"$work"'/home"|' \
    -e 's|^XYMONTMP=.*|XYMONTMP="'"$work"'/home/tmp"|' \
	"$XYMONSERVER_CFG" > "$work/xymonserver.cfg"
printf 'XYMOND_TLS_CERT="%s"\nXYMOND_TLS_KEY="%s"\nXYMOND_TLS_CA="%s"\n' \
	"$work/server.pem" "$work/server.key" "$work/ca.pem" >> "$work/xymonserver.cfg"

MLPID=
"$XYMOND" --no-daemon --listen="127.0.0.1:$P" --tls-listen="127.0.0.1:$T" \
	--hosts="$work/hosts.cfg" --env="$work/xymonserver.cfg" \
	--pidfile="$work/xymond.pid" --checkpoint-file="$work/chk.out" \
	> "$work/xymond.log" 2>&1 &
MLPID=$!
stop() { [ -n "$MLPID" ] || return 0; kill "$MLPID" 2>/dev/null || true; wait "$MLPID" 2>/dev/null || true; MLPID=; }
register_cleanup 'stop'
for _ in $(seq 1 100); do
	grep -q 'Setup complete' "$work/xymond.log" 2>/dev/null && break
	kill -0 "$MLPID" 2>/dev/null || fail "xymond did not start: $(cat "$work/xymond.log")"
	sleep 0.1
done
grep -q 'Setup complete' "$work/xymond.log" || fail "xymond did not start: $(cat "$work/xymond.log")"

# as WHO MESSAGE -- send in plaintext (plain), over TLS without a client
# certificate (none), or over TLS with WHO's certificate
as() {
	local who=$1 m=$2
	case $who in
	plain)	XYMON_TIMEOUT=5 "$XYMONCLIENT" "127.0.0.1:$P" "$m" 2>&1 || true ;;
	none)	env XYMON_TIMEOUT=5 XYMON_TLS_CA="$work/ca.pem" "$XYMONCLIENT" "xymons://127.0.0.1:$T" "$m" 2>&1 || true ;;
	*)	env XYMON_TIMEOUT=5 XYMON_TLS_CA="$work/ca.pem" XYMON_TLS_CERT="$work/$who.pem" XYMON_TLS_KEY="$work/$who.key" \
			"$XYMONCLIENT" "xymons://127.0.0.1:$T" "$m" 2>&1 || true ;;
	esac
}
# transport HOST TEST -- the transport xymond holds for HOST.TEST
transport() { sleep 0.5; as plain "xymondboard host=$1 test=$2 fields=transport" | tr -d '\n'; }

as plain "status web01.example.com.cpu green in plaintext" >/dev/null
got=$(transport web01.example.com cpu)
[ "$got" = "plaintext" ] || fail "a plaintext status reads '$got', not 'plaintext'"

as none "status web01.example.com.disk green over TLS, no certificate" >/dev/null
got=$(transport web01.example.com disk)
case $got in TLSv1.[23]) ;; *) fail "a TLS status without a certificate reads '$got', not the TLS version alone" ;; esac

as web01 "status web01.example.com.memory green with web01's certificate" >/dev/null
got=$(transport web01.example.com memory)
case $got in "TLSv1."[23]" cert") ;; *) fail "a TLS status with web01's certificate, for web01, reads '$got', not '<version> cert'" ;; esac

as web01 "status db01.example.com.cpu green web01's certificate, for db01" >/dev/null
got=$(transport db01.example.com cpu)
case $got in TLSv1.[23]) ;; *) fail "web01's certificate, sending for db01, reads '$got': it does not name db01" ;; esac

as plain "status web01.example.com.memory green back in plaintext" >/dev/null
got=$(transport web01.example.com memory)
[ "$got" = "plaintext" ] || fail "a plaintext report after a TLS one reads '$got', not 'plaintext'"

as web01 "client web01.example.com.linux linux
[date]
with web01's certificate" >/dev/null
as plain "combo
status web01.example.com.procs green derived from the client message
Status message received from 127.0.0.1
" >/dev/null
got=$(transport web01.example.com procs)
case $got in "TLSv1."[23]" cert") ;; *) fail "a status relayed from web01's client message reads '$got', not that message's '<version> cert'" ;; esac

got=$(transport web01.example.com clientlog)
case $got in "TLSv1."[23]" cert") ;; *) fail "web01's client column reads '$got', not its client message's '<version> cert'" ;; esac
got=$(as plain "xymondboard host=web01.example.com test=clientlog fields=sender" | tr -d '\n')
[ "$got" = "127.0.0.1" ] || fail "web01's client column shows sender '$got', not 127.0.0.1"
got=$(as plain "xymondboard host=web01.example.com test=^(info|trends)$ fields=sender,transport" | tr -d '\n')
[ "$got" = "||" ] || [ "$got" = "|" ] || fail "the info and trends columns show '$got': they arrived from nowhere and should show nothing"

as plain "combo
status db01.example.com.procs green relayed, no client message
Status message received from 127.0.0.1
" >/dev/null
got=$(transport db01.example.com procs)
[ -z "$got" ] || fail "a relayed status for db01, which sent no client message, reads '$got', not nothing"

pass "xymond records each status's transport: plaintext, the TLS version, and ' cert' when a verified client certificate names the host; a relayed status, and the client column, take the host's client message transport"
