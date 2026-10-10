#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/xymond-check-tls.sh
#
# xymond --check-tls loads the TLS settings as --tls-listen would, says
# whether they would be served, and exits: 0 when they would, 1 when not.
# It binds nothing and starts nothing, so it can run beside a live xymond,
# before a restart or after a renewal.
#
# Checked: a good certificate and key report how many days are left, and
# whether client certificates are not asked for, asked for, or required;
# a missing certificate, a key anyone may read, a key that is not the
# certificate's, a required certificate without a CA, an expired
# certificate and one not valid yet each exit 1 with the reason. The last
# two are made by xymond-check-tls-harness.c, as a fixture could not stay
# expired-but-not-yet or valid-but-not-yet.
#
# Startup, by contrast, serves an expired certificate with a warning, and
# SIGHUP warns of one not valid yet: refusing to start would stop the
# plaintext port too, for a certificate only the TLS clients need.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
require_bin XYMOND xymond/xymond
require_cc
sslcflags=$(xymon_sslcflags "$ROOT")

# shellcheck source=tests/lib/xymond-daemon.sh
. "$(dirname "$0")/../lib/xymond-daemon.sh"
require_bin XYMONCLIENT common/xymon	# free_port asks it

work=$(mktempdir)
mkdir -p "$work/home/etc" "$work/home/tmp"
P=$(free_port)
require_cfg XYMONSERVER_CFG xymond/etcfiles/xymonserver.cfg

# check [VAR=VALUE...] -- xymond --check-tls with those settings appended to
# xymonserver.cfg; prints its output and then "exit N"
check() {
	local rc=0
	sed -e 's|^XYMONHOME=.*|XYMONHOME="'"$work"'/home"|' \
	    -e 's|^XYMONTMP=.*|XYMONTMP="'"$work"'/home/tmp"|' \
		"$XYMONSERVER_CFG" > "$work/xymonserver.cfg"
	for kv in "$@"; do printf '%s="%s"\n' "${kv%%=*}" "${kv#*=}" >> "$work/xymonserver.cfg"; done
	# --listen on a port of its own: if --check-tls went on to start, it must
	# not collide with anything else. It must exit by itself, and soon.
	"$XYMOND" --check-tls --env="$work/xymonserver.cfg" --listen="127.0.0.1:$P" --no-loopback \
		--pidfile="$work/xymond.pid" > "$work/out" 2>&1 &
	pid=$!
	for _ in $(seq 1 100); do kill -0 "$pid" 2>/dev/null || break; sleep 0.1; done
	if kill -0 "$pid" 2>/dev/null; then
		kill "$pid" 2>/dev/null || true
		wait "$pid" 2>/dev/null || true
		fail "xymond --check-tls did not exit within 10 seconds: $(cat "$work/out")"
	fi
	wait "$pid" || rc=$?
	cat "$work/out"
	echo "exit $rc"
}

case $sslcflags in
*HAVE_OPENSSL*) ;;
*)	got=$(check)
	assert_contains "built without OpenSSL" "$got" "a xymond built without OpenSSL did not say why --check-tls fails"
	assert_contains "exit 1" "$got" "--check-tls exited 0 in a xymond built without OpenSSL"
	pass "a xymond built without OpenSSL fails --check-tls, and says why" ;;
esac

cp "$ROOT"/tests/fixtures/tls/*.pem "$ROOT"/tests/fixtures/tls/*.key "$work/"
chmod 600 "$work"/*.key
# shellcheck disable=SC2046,SC2086  # flag lists, split on purpose
"$CC" $(xymon_cflags "$ROOT") $sslcflags -o "$work/harness" "$(dirname "$0")/xymond-check-tls-harness.c" \
	$(xymon_ldflags "$ROOT") -lssl -lcrypto 2>"$work/cc.log" \
	|| fail "the harness does not build: $(cat "$work/cc.log")"
"$work/harness" "$work/expired" -30 -1 || fail "the harness could not make an expired certificate"
"$work/harness" "$work/future" 2 30 || fail "the harness could not make a certificate not valid yet"
chmod 600 "$work"/expired.key "$work"/future.key

good="XYMOND_TLS_CERT=$work/server.pem XYMOND_TLS_KEY=$work/server.key"
# shellcheck disable=SC2086  # $good is two settings
got=$(check $good)
assert_contains "exit 0" "$got" "a good certificate and key failed the check"
assert_contains "loads with its key and expires in" "$got" "the check did not report the certificate's days left"
assert_contains "client certificates are not asked for" "$got" "the check did not say no client certificate is asked for"
assert_not_contains "Listening on" "$got" "--check-tls bound a listener"
# shellcheck disable=SC2086
assert_contains "client certificates are asked for" "$(check $good "XYMOND_TLS_CA=$work/ca.pem")" "the check did not report XYMOND_TLS_CA"
# shellcheck disable=SC2086
assert_contains "client certificates are required" "$(check $good "XYMOND_TLS_CA=$work/ca.pem" XYMOND_TLS_REQUIRE_CERT=TRUE)" \
	"the check did not report XYMOND_TLS_REQUIRE_CERT"

# expect_fail REASON CASE VAR=VALUE... -- the check exits 1, saying REASON
expect_fail() {
	local reason=$1 what=$2 got
	shift 2
	got=$(check "$@")
	assert_contains "exit 1" "$got" "the check passed $what"
	assert_contains "$reason" "$got" "the check failed $what without saying why"
}
expect_fail "$work/missing.pem" "a missing certificate" "XYMOND_TLS_CERT=$work/missing.pem" "XYMOND_TLS_KEY=$work/server.key"
cp "$work/server.key" "$work/open.key"
chmod 644 "$work/open.key"
expect_fail "may be read by anyone" "a key anyone may read" "XYMOND_TLS_CERT=$work/server.pem" "XYMOND_TLS_KEY=$work/open.key"
expect_fail "$work/web01.key" "a key that is not the certificate's" "XYMOND_TLS_CERT=$work/server.pem" "XYMOND_TLS_KEY=$work/web01.key"
expect_fail "needs a CA" "a required client certificate without a CA" "XYMOND_TLS_CERT=$work/server.pem" "XYMOND_TLS_KEY=$work/server.key" XYMOND_TLS_REQUIRE_CERT=TRUE
expect_fail "has expired" "an expired certificate" "XYMOND_TLS_CERT=$work/expired.pem" "XYMOND_TLS_KEY=$work/expired.key"
expect_fail "is not valid yet" "a certificate not valid yet" "XYMOND_TLS_CERT=$work/future.pem" "XYMOND_TLS_KEY=$work/future.key"

# Startup and SIGHUP warn, and xymond keeps running
require_shm_segments "$(grep -c 'setup_channel(C_[A-Z_]*, CHAN_MASTER)' "$ROOT/xymond/xymond.c")"
T=$(free_port)
printf 'page test Test\n127.0.0.1 testhost # conn\n' > "$work/hosts.cfg"
printf 'XYMOND_TLS_CERT="%s"\nXYMOND_TLS_KEY="%s"\n' "$work/expired.pem" "$work/expired.key" >> "$work/xymonserver.cfg"
"$XYMOND" --no-daemon --listen="127.0.0.1:$P" --no-loopback --tls-listen="127.0.0.1:$T" \
	--hosts="$work/hosts.cfg" --env="$work/xymonserver.cfg" \
	--pidfile="$work/xymond.pid" --checkpoint-file="$work/chk.out" > "$work/xymond.log" 2>&1 &
xpid=$!
register_cleanup "kill $xpid 2>/dev/null || true"
for _ in $(seq 1 100); do
	grep -q 'Setup complete' "$work/xymond.log" 2>/dev/null && break
	kill -0 "$xpid" 2>/dev/null || fail "xymond did not start with an expired certificate: $(cat "$work/xymond.log")"
	sleep 0.1
done
grep -q "TLS: WARNING: $work/expired.pem has expired" "$work/xymond.log" \
	|| fail "xymond started with an expired certificate without a warning: $(cat "$work/xymond.log")"
case "$(XYMON_TIMEOUT=5 "$XYMONCLIENT" "127.0.0.1:$P" ping 2>&1 || true)" in
*xymond*) ;;
*)	fail "xymond with an expired TLS certificate does not answer on its plaintext port" ;;
esac
cp "$work/future.pem" "$work/expired.pem"
cp "$work/future.key" "$work/expired.key"
kill -HUP "$xpid"
sleep 0.5
grep -q "TLS: WARNING: $work/expired.pem is not valid yet" "$work/xymond.log" \
	|| fail "SIGHUP loaded a certificate not valid yet without a warning: $(tail -3 "$work/xymond.log")"
kill -0 "$xpid" 2>/dev/null || fail "xymond stopped after loading a certificate not valid yet on SIGHUP"

pass "xymond --check-tls passes good TLS settings with what it found, and fails each broken one with the reason; startup and SIGHUP warn of a certificate out of its dates, and serve"
