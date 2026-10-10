#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/xymond-tls-listen.sh
#
# xymond takes TLS connections on the addresses --tls-listen names, with the
# certificate XYMOND_TLS_CERT and XYMOND_TLS_KEY name.
#
# The xymon client sends to xymons://, and checked are:
#
#   - a status sent over TLS is kept, and a query over TLS is answered --
#     a small message, and one of ~400 KB: many TLS records, and past the
#     128 KB xymond starts a connection's buffer with, so it grows;
#   - twenty clients at once over TLS each have their status kept;
#   - a client that speaks plaintext to the TLS port, or that closes the
#     connection without close_notify, delivers nothing, and xymond says why;
#   - an entry without a port listens on XYMONDTLSPORT;
#   - SIGHUP loads a renewed certificate, and keeps the old one when the new
#     one does not load;
#   - xymond does not start when the certificate does not load, or when its
#     private key may be read by anyone;
#   - an IPv6 address listens for TLS too.
#
# A xymond built without OpenSSL must refuse --tls-listen instead, and that
# is what is checked there.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
# shellcheck source=tests/lib/xymond-daemon.sh
. "$(dirname "$0")/../lib/xymond-daemon.sh"

ROOT=$(find_root)
require_bin XYMOND xymond/xymond
require_bin XYMONCLIENT common/xymon
require_cc
require_shm_segments "$(grep -c 'setup_channel(C_[A-Z_]*, CHAN_MASTER)' "$ROOT/xymond/xymond.c")"

work=$(mktempdir)
mkdir -p "$work/home/etc" "$work/home/tmp" "$work/home/www"
printf 'page test Test\n127.0.0.1 testhost # conn\n' > "$work/hosts.cfg"
cp "$ROOT"/tests/fixtures/tls/*.pem "$ROOT"/tests/fixtures/tls/*.key "$work/"
chmod 600 "$work"/*.key
cp "$work/server.pem" "$work/xymond.pem"
cp "$work/server.key" "$work/xymond.key"

P=$(free_port)
T=$(free_port)
T2=$(free_port)

require_cfg XYMONSERVER_CFG xymond/etcfiles/xymonserver.cfg
sed -e 's|^XYMONHOME=.*|XYMONHOME="'"$work"'/home"|' \
    -e 's|^XYMONTMP=.*|XYMONTMP="'"$work"'/home/tmp"|' \
	"$XYMONSERVER_CFG" > "$work/xymonserver.cfg"
# MAXMSG_STATUS: room for the ~400 KB status, which the 256 KB default truncates
printf 'XYMOND_TLS_CERT="%s"\nXYMOND_TLS_KEY="%s"\nXYMONDTLSPORT="%s"\nMAXMSG_STATUS="1024"\n' \
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

# tls RECIPIENT MESSAGE [VAR=VALUE...] -- the xymon client over TLS, trusting the test CA
sslcflags=$(xymon_sslcflags "$ROOT")
case $sslcflags in
*HAVE_OPENSSL*) ;;
*)	if launch --tls-listen="127.0.0.1:$T"; then fail "a xymond built without OpenSSL started with --tls-listen"; fi
	assert_contains "built without OpenSSL" "$(cat "$work/xymond.log")" "xymond did not say why it refused --tls-listen"
	pass "a xymond built without OpenSSL refuses --tls-listen" ;;
esac

# shellcheck disable=SC2046,SC2086  # flag lists, split on purpose
"$CC" $(xymon_cflags "$ROOT") $sslcflags -iquote "$ROOT/lib" -o "$work/harness" \
	"$(dirname "$0")/xymond-tls-listen-harness.c" "$ROOT/lib/libxymoncomm.a" \
	$(xymon_ldflags "$ROOT") -lssl -lcrypto 2>"$work/cc.log" \
	|| fail "the harness does not build: $(cat "$work/cc.log")"

tls() {
	local r=$1 m=$2
	shift 2
	env XYMON_TIMEOUT=5 XYMON_TLS_CA="$work/ca.pem" "$@" "$XYMONCLIENT" "$r" "$m" 2>&1 || true
}
plain() { XYMON_TIMEOUT=5 "$XYMONCLIENT" "127.0.0.1:$P" "$1" 2>&1 || true; }

launch --tls-listen="127.0.0.1:$T" || fail "xymond did not start with --tls-listen: $(cat "$work/xymond.log")"
grep -q "Listening on 127.0.0.1:$T for TLS" "$work/xymond.log" || fail "xymond did not report its TLS listener: $(cat "$work/xymond.log")"

tls "xymons://127.0.0.1:$T" "status testhost.cpu green sent over TLS" >/dev/null
sleep 0.5
assert_contains "sent over TLS" "$(plain "xymondlog testhost.cpu")" "a status sent over TLS was not kept: $(cat "$work/xymond.log")"
assert_contains "sent over TLS" "$(tls "xymons://127.0.0.1:$T" "xymondlog testhost.cpu")" "a query over TLS was not answered"

# ~400 KB: many TLS records (16 KB each at most), past xymond's 128 KB
# starting buffer, the last of them the end marker. On stdin ("@"), as the
# client script sends: one argument of this size is more than exec takes.
{
	echo "status testhost.disk green long message"
	printf 'line of filler text for a long status message %05d\n' $(seq 1 8000)
	echo "end of the long message"
} > "$work/big"
env XYMON_TIMEOUT=10 XYMON_TLS_CA="$work/ca.pem" "$XYMONCLIENT" "xymons://127.0.0.1:$T" "@" < "$work/big" >/dev/null 2>&1 || true
sleep 0.5
assert_contains "end of the long message" "$(plain "xymondlog testhost.disk")" \
	"a status of $(wc -c < "$work/big" | tr -d ' ') bytes over TLS was not kept whole: $(tail -5 "$work/xymond.log")"
assert_contains "end of the long message" "$(tls "xymons://127.0.0.1:$T" "xymondlog testhost.disk")" \
	"the long status, asked for over TLS, did not come back whole"

# Twenty clients at once, each its own column (waiting for them, not for xymond)
cpids=""
for i in $(seq 1 20); do
	tls "xymons://127.0.0.1:$T" "status testhost.c$i green concurrent client $i" >/dev/null &
	cpids="$cpids $!"
done
for p in $cpids; do wait "$p" || true; done
sleep 0.5
for i in $(seq 1 20); do
	assert_contains "concurrent client $i" "$(plain "xymondlog testhost.c$i")" \
		"of twenty TLS clients at once, client $i's status was not kept: $(tail -5 "$work/xymond.log")"
done

XYMON_TIMEOUT=5 "$XYMONCLIENT" "127.0.0.1:$T" "status testhost.plain green plaintext to the TLS port" >/dev/null 2>&1 || true
"$work/harness" 127.0.0.1 "$T" "status testhost.cut green never ended" >/dev/null || fail "the harness could not reach the TLS port"
sleep 0.5
assert_not_contains "plaintext to the TLS port" "$(plain "xymondlog testhost.plain")" "a plaintext status on the TLS port was kept"
assert_not_contains "never ended" "$(plain "xymondlog testhost.cut")" "a status without close_notify was kept"
grep -q 'TLS handshake from 127.0.0.1 failed' "$work/xymond.log" \
	|| fail "xymond did not say why it dropped plaintext on the TLS port: $(cat "$work/xymond.log")"
grep -q 'TLS message from 127.0.0.1 failed' "$work/xymond.log" \
	|| fail "xymond did not say why it dropped a message without close_notify: $(cat "$work/xymond.log")"

# SIGHUP: renewed.pem replaces the server's certificate, then a broken one
# does not. A client trusting only renewed.pem connects only once it is served.
renewed() { tls "xymons://127.0.0.1:$T" "ping" XYMON_TLS_CA="$work/renewed.pem"; }
assert_not_contains "xymond" "$(renewed)" "a client trusting only renewed.pem connected before the reload"
cp "$work/renewed.pem" "$work/xymond.pem"
cp "$work/renewed.key" "$work/xymond.key"
kill -HUP "$MLPID"
sleep 0.5
assert_contains "xymond" "$(renewed)" "after SIGHUP, xymond did not serve the renewed certificate: $(tail -3 "$work/xymond.log")"
echo "not a certificate" > "$work/xymond.pem"
kill -HUP "$MLPID"
sleep 0.5
grep -q 'TLS: keeping the certificate loaded before' "$work/xymond.log" \
	|| fail "xymond did not say it kept the certificate when the new one failed: $(tail -5 "$work/xymond.log")"
assert_contains "xymond" "$(renewed)" "a certificate that does not load replaced the one in use"
stop

# An entry without a port: XYMONDTLSPORT
cp "$work/server.pem" "$work/xymond.pem"
cp "$work/server.key" "$work/xymond.key"
launch --tls-listen=127.0.0.1 || fail "xymond did not start with --tls-listen=127.0.0.1: $(cat "$work/xymond.log")"
assert_contains "xymond" "$(tls "xymons://127.0.0.1:$T2" ping)" "--tls-listen=127.0.0.1 does not listen on XYMONDTLSPORT $T2: $(cat "$work/xymond.log")"
stop

# No TLS port that cannot give TLS
mv "$work/xymond.pem" "$work/missing.pem"
if launch --tls-listen="127.0.0.1:$T"; then fail "xymond started with a certificate file that does not exist"; fi
grep -q "TLS: .*$work/xymond" "$work/xymond.log" || fail "xymond did not say which file failed: $(cat "$work/xymond.log")"
mv "$work/missing.pem" "$work/xymond.pem"
chmod 644 "$work/xymond.key"
if launch --tls-listen="127.0.0.1:$T"; then fail "xymond started with a private key anyone may read"; fi
assert_contains "may be read by anyone" "$(cat "$work/xymond.log")" "xymond did not say why the key was refused"
chmod 600 "$work/xymond.key"

if ! launch --tls-listen="[::1]:$T"; then
	grep -q 'Cannot bind to listen socket ::1' "$work/xymond.log" || fail "xymond did not start with --tls-listen=[::1]:$T: $(cat "$work/xymond.log")"
	pass_partial "xymond takes TLS on IPv4, reads a message to its close_notify, drops one without, and reloads its certificate on SIGHUP" \
		"IPv6: this host has no IPv6 loopback ($(grep -o 'Cannot bind.*' "$work/xymond.log"))"
fi
assert_contains "xymond" "$(tls "xymons://[::1]:$T" ping)" "xymond does not answer TLS on [::1]:$T"
stop

pass "xymond takes TLS on IPv4 and IPv6, reads a message to its close_notify, drops one without, and reloads its certificate on SIGHUP"
