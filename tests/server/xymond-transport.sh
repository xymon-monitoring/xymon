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
#     most recent report.
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

pass "xymond records each status's transport: plaintext, the TLS version, and ' cert' when a verified client certificate names the host"
