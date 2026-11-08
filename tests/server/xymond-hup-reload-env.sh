#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/xymond-hup-reload-env.sh
#
# On SIGHUP xymond re-reads its --env file, so a TLS setting changed there takes
# effect. Here that is the XYMOND_TLS_CERT and XYMOND_TLS_KEY path: pointing it
# at a different certificate and sending SIGHUP serves that certificate. Before
# this, the reload read the paths from the environment loaded at startup, so a
# changed path was ignored -- only a renewed file's contents at the original
# path were picked up.
#
# Driven against a running xymond --tls-listen: it starts serving server.pem; a
# client that trusts only renewed.pem cannot connect. The --env file is then
# pointed at renewed.pem (a different certificate, at its own path, server.pem
# left untouched) and SIGHUP'd, and that client connects. A path that does not
# load is set next, and the certificate in use is kept.
#
# Needs a built tree with OpenSSL: xymond and the xymon client.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
# shellcheck source=tests/lib/xymond-daemon.sh
. "$(dirname "$0")/../lib/xymond-daemon.sh"

ROOT=$(find_root)
require_bin XYMOND xymond/xymond
require_bin XYMONCLIENT common/xymon
require_shm_segments "$(grep -c 'setup_channel(C_[A-Z_]*, CHAN_MASTER)' "$ROOT/xymond/xymond.c")"

case $(xymon_sslcflags "$ROOT") in
*HAVE_OPENSSL*) ;;
*) skip "xymond built without OpenSSL" ;;
esac

work=$(mktempdir)
mkdir -p "$work/home/etc" "$work/home/tmp" "$work/home/www"
printf 'page test Test\n127.0.0.1 testhost # conn\n' > "$work/hosts.cfg"
cp "$ROOT"/tests/fixtures/tls/*.pem "$ROOT"/tests/fixtures/tls/*.key "$work/"
chmod 600 "$work"/*.key
cp "$work/server.pem" "$work/xymond.pem"
cp "$work/server.key" "$work/xymond.key"

P=$(free_port); T=$(free_port); T2=$(free_port)

require_cfg XYMONSERVER_CFG xymond/etcfiles/xymonserver.cfg
sed -e 's|^XYMONHOME=.*|XYMONHOME="'"$work"'/home"|' \
    -e 's|^XYMONTMP=.*|XYMONTMP="'"$work"'/home/tmp"|' \
	"$XYMONSERVER_CFG" > "$work/xymonserver.cfg"
printf 'XYMOND_TLS_CERT="%s"\nXYMOND_TLS_KEY="%s"\nXYMONDTLSPORT="%s"\n' \
	"$work/xymond.pem" "$work/xymond.key" "$T2" >> "$work/xymonserver.cfg"

MLPID=
launch() {
	"$XYMOND" --no-daemon --listen="127.0.0.1:$P" "$@" \
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

# A ping over TLS, trusting only renewed.pem: it connects only when xymond
# serves that certificate, which tells it apart from server.pem.
renewed() { env XYMON_TIMEOUT=5 XYMON_TLS_CA="$work/renewed.pem" "$XYMONCLIENT" "xymons://127.0.0.1:$T" "ping" 2>&1 || true; }

launch --tls-listen="127.0.0.1:$T" || fail "xymond did not start with --tls-listen: $(cat "$work/xymond.log")"

assert_not_contains "xymond" "$(renewed)" \
	"a client trusting only renewed.pem connected before the --env change"

# Point the --env file at renewed.pem/key. loadenv() is last-wins, so appending
# overrides the earlier lines; server.pem is left on disk untouched, so only a
# re-read of the --env file -- not of the startup path's contents -- can serve
# the new certificate.
printf 'XYMOND_TLS_CERT="%s"\nXYMOND_TLS_KEY="%s"\n' "$work/renewed.pem" "$work/renewed.key" >> "$work/xymonserver.cfg"
kill -HUP "$MLPID"
sleep 0.5
assert_contains "xymond" "$(renewed)" \
	"after SIGHUP, the changed XYMOND_TLS_CERT path was not picked up: $(tail -3 "$work/xymond.log")"

# A path that does not load keeps the certificate in use (renewed.pem).
printf 'XYMOND_TLS_CERT="%s"\n' "$work/absent.pem" >> "$work/xymonserver.cfg"
kill -HUP "$MLPID"
sleep 0.5
grep -q 'TLS: keeping the certificate loaded before' "$work/xymond.log" \
	|| fail "xymond did not keep the certificate when the new path failed: $(tail -5 "$work/xymond.log")"
assert_contains "xymond" "$(renewed)" \
	"a certificate path that does not load replaced the one in use"
stop

pass "SIGHUP re-reads the --env file, so a changed XYMOND_TLS_CERT path takes effect, and a path that does not load keeps the certificate in use"
