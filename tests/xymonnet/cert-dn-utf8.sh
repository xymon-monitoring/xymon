#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/xymonnet/cert-dn-utf8.sh
#
# The sslcert status shows a certificate's subject and issuer. xymonnet built
# those strings with X509_NAME_oneline(), which OpenSSL strongly discourages:
# it escapes every byte >= 0x80 as "\xHH", so an accented field such as
# "O=Societe" with its e's accented came out as "O=Soci\xC3\xA9t\xC3\xA9".
#
# cert_dn_string() now uses X509_NAME_print_ex() with XN_FLAG_RFC2253 minus
# ASN1_STRFLGS_ESC_MSB: the RFC 2253 form, with the UTF-8 kept. The harness
# makes a subject with an accented UTF-8 "O=" field and prints the result; it
# must carry the field as UTF-8, and no "\x" escape.

set -euo pipefail
# The output is grepped for byte sequences; BSD grep aborts an invalid-UTF-8
# match under a UTF-8 locale, so run byte-oriented.
export LC_ALL=C
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
	*) skip "built without OpenSSL: cert_dn_string() is not compiled" ;;
esac

harness_cflags=$(xymon_cflags "$ROOT")
harness_ldflags=$(xymon_ldflags "$ROOT")
# shellcheck disable=SC2046  # each make variable is a list of words
"$CC" $harness_cflags $sslflags $(xymon_makevar "$ROOT" SSLINCDIR) \
	$(xymon_makevar "$ROOT" CARESINCDIR) -o "$work/harness" \
	"$ROOT/tests/xymonnet/cert-dn-utf8-harness.c" \
	"$ROOT/xymonnet/httptest.o" "$ROOT/xymonnet/dns.o" "$ROOT/xymonnet/dns2.o" "$ROOT/xymonnet/httpcookies.o" \
	$(xymon_makevar "$ROOT" CARESLIBS) "$(xymon_lib "$ROOT" libxymoncomm.a)" "$(xymon_lib "$ROOT" libxymontime.a)" \
	$(xymon_makevar "$ROOT" ZLIBLIBS) $(xymon_makevar "$ROOT" SSLLIBS) $(xymon_makevar "$ROOT" NETLIBS) \
	$(xymon_makevar "$ROOT" LIBRTDEF) $harness_ldflags 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; fail "the harness does not build against xymonnet's objects"; }

"$work/harness" > "$work/out" 2> "$work/err" || fail "the harness could not run: $(cat "$work/err")"

eac=$(printf 'Soci\303\251t\303\251')     # "Societe" with UTF-8 e-acutes
assert_contains "O=$eac" "$(cat "$work/out")" \
	"the subject's accented O= field is not carried as UTF-8: $(cat "$work/out")"
assert_not_contains '\x' "$(cat "$work/out")" \
	"a byte is still escaped as \\xHH: $(cat "$work/out")"
# RFC 2253 is most-specific first, comma-separated: CN leads, no leading slash.
assert_contains "CN=mail.example.com," "$(cat "$work/out")" \
	"the DN is not in RFC 2253 form: $(cat "$work/out")"

pass "cert_dn_string() renders the certificate DN as RFC 2253, keeping an accented field as UTF-8"
