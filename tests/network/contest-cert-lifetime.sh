#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/network/contest-cert-lifetime.sh
#
# Nothing in setup_ssl() may read the peer certificate after releasing it.
#
# The certificate block ends with X509_free(peercert). The cipher-list section
# that follows (entered whenever sslincludecipherlist is set, the default) then
# recomputed the signature algorithm from that certificate -- a read through a
# pointer this function no longer owns, and a dead one: certsigalg was already
# emitted above and the new value was never used.
#
# Not a use-after-free today: SSL_get_peer_certificate() returns an
# incremented reference, so the session still holds one and the object
# survives. It becomes one the moment the session is released first, which is
# why the ordering is worth pinning rather than left to hold by accident.
#
# setup_ssl() needs the OpenSSL headers, a real handshake and the network to
# run, so -- like tests/server/combostatus-overflow.sh -- this binds to the
# source: the freed certificate must not be read again. The invariant checked
# is stronger than "delete one line": X509_free(peercert) must be the last
# code mention of peercert in the file, so a future read of the certificate
# through that name fails here too. It is textual, so it does not catch a
# use-after-free routed through a different pointer aliased to peercert -- that
# needs a real handshake under ASan, out of reach for an in-tree guard.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
SRC="$ROOT/xymonnet/contest.c"

[ -f "$SRC" ] || skip "xymonnet/contest.c absent"

# The specific dead read that caused it must be gone.
grep -q 'X509_get_signature_type(peercert)' "$SRC" \
	&& fail "contest.c reads peercert with X509_get_signature_type after it is freed (use-after-free)"

# There must be exactly one free of peercert...
frees=$(grep -c 'X509_free(peercert)' "$SRC")
[ "$frees" -eq 1 ] || fail "contest.c has $frees X509_free(peercert) calls, expected exactly 1"

# ...and it must be the last time peercert is named: nothing may touch the
# certificate after it is freed.
free_line=$(grep -n 'X509_free(peercert)' "$SRC" | cut -d: -f1)
# Ignore comment-prefixed lines: a future comment naming peercert after the
# free is harmless and must not read as a use-after-free.
last_line=$(grep -n 'peercert' "$SRC" | grep -vE ':[[:space:]]*(\*|/\*|//)' | tail -1 | cut -d: -f1)
[ "$free_line" = "$last_line" ] \
	|| fail "contest.c references peercert at line $last_line, after it is freed at line $free_line (use-after-free)"

pass "contest.c frees the peer certificate last -- nothing reads it after X509_free(peercert)"
