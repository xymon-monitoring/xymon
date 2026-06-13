#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/network/alpn-support.sh
#
# Guard for the xymonnet ALPN support added by xymon-monitoring/xymon#37.
#
# #37 lets a protocols.cfg service negotiate an ALPN protocol during the TLS
# handshake (e.g. `options ssl,alpn=imap`). The plumbing spans three layers:
#   - lib/netservices.h : the TCP_ALPN flag + the `alpns` field,
#   - lib/netservices.c : parsing the `alpn=` option into that field,
#   - xymonnet/contest.c: handing the parsed list to OpenSSL via
#     SSL_CTX_set_alpn_protos() during SSL setup.
#
# Exercising it for real needs xymonnet built plus a TLS server advertising
# ALPN; the build CI already compiles these files, so this is a static guard
# that the wiring across the three layers (and the manpage) survives future
# edits -- e.g. the netservices/contest rework or the CMake migration. Skips
# only when the source files are absent (e.g. an autopkgtest run against an
# installed package with no source tree); if a present tree has lost the ALPN
# wiring, that is a regression and the test fails rather than skipping green.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)

# Server-only: ALPN lives in xymonnet, which client/localclient builds omit.
need_variant server

HDR="$ROOT/lib/netservices.h"
SRC="$ROOT/lib/netservices.c"
SSL="$ROOT/xymonnet/contest.c"
MAN="$ROOT/xymonnet/protocols.cfg.5"

for f in "$HDR" "$SRC" "$SSL" "$MAN"; do
	[ -f "$f" ] || skip "$(basename "$f") absent"
done

hdr=$(cat "$HDR")
src=$(cat "$SRC")

# These source files ship in the same tree as this test, so missing ALPN
# wiring means someone removed it -- a regression (e.g. a partial removal
# during the netservices/contest rework), not a pre-feature tree -- and must
# fail, never skip green. The only legitimate skip is an absent *environment*
# (the source files themselves not present, handled above), not absent
# project code. Assert every layer unconditionally.

# header: the flag and the field that carries the negotiated protocol list
assert_contains "TCP_ALPN" "$hdr" "netservices.h lost the TCP_ALPN flag (#37)"
assert_contains "char *alpns" "$hdr" "netservices.h lost the svcinfo alpns field (#37)"

# parser: the alpn= option is read INTO the alpns field. A bare grep for
# "alpns" is a false green -- the field declaration and the later copy-out to
# svcinfo both mention it, so the parser store can be deleted while the word
# survives. Pin the assertion to the assignment of the parsed value into the
# alpns field, which is the line that actually wires parsing up.
assert_contains '"alpn=", 5' "$src" "netservices.c no longer parses the alpn= option (#37)"
# The stored value must be opt+5, not opt: "alpn=" is 5 chars, so dropping the
# +5 stores the literal "alpn=imap" instead of "imap" -- a real parser bug.
# Pin the offset so the strdup(opt+5) -> strdup(opt) mutation fails here; a
# bare strdup(opt...) match would accept the broken form as a false green.
grep -Eq 'alpns[[:space:]]*=[[:space:]]*strdup\(opt[[:space:]]*\+[[:space:]]*5[[:space:]]*\)' "$SRC" \
	|| fail "netservices.c no longer stores the parsed alpn= value (opt+5) into the alpns field (#37)"

# SSL setup: the parsed list must actually FLOW to OpenSSL, not merely have the
# call present. A bare grep for SSL_CTX_set_alpn_protos is a false green -- the
# call survives even if the source assignments are nulled (alpn_protocols left
# NULL) or the call is fed a literal NULL, both of which silently disable ALPN
# while the symbol stays. Pin both ends of the data flow: the parsed alpns is
# read into the protocol list, and the packed buffer (not NULL) feeds the call.
grep -Eq 'alpn_protocols[[:space:]]*=[[:space:]]*item->(svcinfo|ssloptions)->alpns' "$SSL" \
	|| fail "contest.c no longer reads the parsed alpns into the ALPN protocol list (#37)"
grep -Eq 'SSL_CTX_set_alpn_protos\(item->sslctx, *alpn_buffer,' "$SSL" \
	|| fail "contest.c no longer hands the packed ALPN buffer to OpenSSL (#37)"

# manpage documents the option. The page ships in this tree and its absence
# already skipped the whole test above, so assert unconditionally -- a present
# tree that lost the docs is a regression, not a legitimate skip.
assert_contains "alpn=" "$(cat "$MAN")" \
	"protocols.cfg.5 no longer documents the alpn= option (#37)"

pass "xymonnet keeps the #37 ALPN wiring (flag, parser, SSL setup, manpage)"
