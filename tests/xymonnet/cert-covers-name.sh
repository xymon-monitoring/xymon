#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/xymonnet/cert-covers-name.sh
#
# The sslcert name check answers as xymonnet(1) documents, on every OpenSSL.
#
# cert_covers_name() decides whether the certificate a server presented covers
# the name sent as SNI: its subjectAltName DNS entries, wildcards included,
# and its subject CN only when it has no DNS subjectAltName -- for a
# certificate xymonnet does not trust, a self-signed or an expired one too.
# It verifies the certificate for the name alone, since OpenSSL 4.1
# deprecates the name check on its own, X509_check_host(). OpenSSL 4.1 also
# stopped matching the CN by default, and the flag that asks for it matches
# the CN beside a subjectAltName on every version, so the flag is set only
# for a certificate without a DNS subjectAltName.
#
# The certificate is made its own trust anchor, since LibreSSL ends
# verification at a chain it cannot build and never reaches the name.
#
# The harness runs it on certificates it makes, and on a CA-signed one that
# nothing trusts, and prints every answer that is not the documented one.
# Before OpenSSL 4.1 the CN-only cases pass without the flag; the case of a
# CN beside a subjectAltName fails, on every version, if it is set always.
# Without the trust anchor the untrusted cases fail on LibreSSL only.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
ROOT=$(find_root)

CC=${CC:-cc}
command -v "$CC" >/dev/null 2>&1 || skip "no C compiler available (CC=$CC)"
work=$(mktempdir)

# Built as xymonnet/Makefile builds xymonnetprobe: contest.c with the objects
# and archives it links. All must already be there -- no make from here.
for f in xymonnet/httptest.o xymonnet/dns.o xymonnet/dns2.o xymonnet/httpcookies.o; do
	[ -f "$ROOT/$f" ] || skip "xymonnet is not built ($f missing)"
done
[ -f "$(xymon_lib "$ROOT" libxymoncomm.a)" ] && [ -f "$(xymon_lib "$ROOT" libxymontime.a)" ] \
	|| skip "the server archives are not built (lib/libxymoncomm.a, lib/libxymontime.a)"
sslflags=$(xymon_makevar "$ROOT" SSLFLAGS)
case $sslflags in
	*-DHAVE_OPENSSL*) ;;
	*) skip "built without OpenSSL: cert_covers_name() is not compiled" ;;
esac

harness_cflags=$(xymon_cflags "$ROOT")
harness_ldflags=$(xymon_ldflags "$ROOT")
# shellcheck disable=SC2046  # each make variable is a list of words
"$CC" $harness_cflags $sslflags $(xymon_makevar "$ROOT" SSLINCDIR) \
	$(xymon_makevar "$ROOT" CARESINCDIR) -o "$work/harness" \
	"$ROOT/tests/xymonnet/cert-covers-name-harness.c" \
	"$ROOT/xymonnet/httptest.o" "$ROOT/xymonnet/dns.o" "$ROOT/xymonnet/dns2.o" "$ROOT/xymonnet/httpcookies.o" \
	$(xymon_makevar "$ROOT" CARESLIBS) "$(xymon_lib "$ROOT" libxymoncomm.a)" "$(xymon_lib "$ROOT" libxymontime.a)" \
	$(xymon_makevar "$ROOT" ZLIBLIBS) $(xymon_makevar "$ROOT" SSLLIBS) $(xymon_makevar "$ROOT" NETLIBS) \
	$(xymon_makevar "$ROOT" LIBRTDEF) $harness_ldflags 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; fail "the harness does not build against xymonnet's objects"; }

rc=0
"$work/harness" "$ROOT/tests/fixtures/tls/server.pem" > "$work/out" 2> "$work/err" || rc=$?
[ "$rc" -le 1 ] || fail "the harness could not run (rc=$rc): $(cat "$work/err")"
assert_equal "0 mismatch(es)" "$(cat "$work/out")" "the sslcert name check gave an answer xymonnet(1) does not document"

pass "the sslcert name check covers subjectAltNames, wildcards and a CN-only certificate as documented, trusted or not"
