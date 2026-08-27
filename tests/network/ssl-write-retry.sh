#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/network/ssl-write-retry.sh
#
# Guard for xymon-monitoring/xymon#451: when SSL_write() accepts nothing,
# xymonnet must retry the same buffer rather than drop the payload.
#
# socket_write() used to translate SSL_ERROR_WANT_READ/WANT_WRITE into a bare
# `res = 0` return. At the call site 0 is neither the -1 error case nor the
# TCP_HTTP short-write case, so for every non-HTTP service the bytes were
# silently discarded -- and because readpending had already been set, the test
# then sat waiting for a reply to a command that was never transmitted. HTTP
# escaped only by accident: sendlen never decreased, so its own branch retried.
#
# Four things have to hold together, and each is a separate way to reintroduce
# the bug, so each is asserted separately:
#   1. the flag exists,
#   2. socket_write() sets it on WANT_READ/WANT_WRITE,
#   3. the call site acts on it *before* the TCP_HTTP branch,
#   4. the shutdown leaves a socket that still owes a write open -- without
#      this the retry can never happen, and (1)-(3) are decoration.
#
# Static guard: reaching WANT_WRITE through a real probe needs a stalled peer
# and a multi-megabyte payload, while the shipped probes send a few bytes --
# which is exactly why the bug was latent. Skips only if the source is absent.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
SRC="$ROOT/xymonnet/contest.c"
HDR="$ROOT/xymonnet/contest.h"

for f in "$SRC" "$HDR"; do
	[ -f "$f" ] || skip "$(basename "$f") absent"
done

grep -Eq '^[[:space:]]*int[[:space:]]+sendagain;' "$HDR" || fail \
	"contest.h lost the sendagain flag -- a dropped write cannot be signalled (#451)"

# contest.c defines socket_write() twice: once for builds without OpenSSL and
# once for builds with it. Only the second has anything to retry, so select the
# body by content rather than by position -- picking the first would assert
# against the plain-write stub and pass no matter what the SSL one does.
# Strip comments before asserting. Every check below is a string-presence
# check, and this function and its call site are heavily commented with the
# very identifiers under assertion -- so a commented-out `item->sendagain = 1`,
# or prose mentioning sendagain, would satisfy a claim about the code beside
# it. Deleting a line and leaving the comment that explains it is the realistic
# edit, and it is exactly the one a token grep cannot see.
strip_comments() { sed -e 's,/\*.*\*/,,' -e '/^[[:space:]]*\/\*/d' -e '/^[[:space:]]*\*/d'; }

ssl_write_fn=$(awk '
	/^static int socket_write/ { buf = ""; inbody = 1 }
	inbody                     { buf = buf $0 "\n" }
	inbody && /^}/             { if (buf ~ /SSL_write/) { printf "%s", buf; exit } inbody = 0 }
' "$SRC" | strip_comments)
[ -n "$ssl_write_fn" ] || fail \
	"contest.c has no socket_write() that calls SSL_write() (#451)"

grep -q 'sendagain = 1' <<<"$ssl_write_fn" || fail \
	"socket_write() no longer flags a write that sent nothing -- the payload is dropped silently (#451)"

# Scoped to WANT_WRITE on purpose. Clearing readpending puts the fd in writefds,
# which is what WANT_WRITE waits for; retrying a WANT_READ there would select on
# a set that is always ready and busy-wait. If the two labels are ever collapsed
# back together, that is the bug this pins.
want_write_arm=$(awk '/case SSL_ERROR_WANT_WRITE:/{c=1} c{print} c&&/break;/{exit}' <<<"$ssl_write_fn")
grep -q 'sendagain = 1' <<<"$want_write_arm" || fail \
	"socket_write() no longer sets sendagain under SSL_ERROR_WANT_WRITE (#451)"
# ...and must return 0, not the negative SSL_write() gave back: the call site
# tests res == -1 first, so a deferred write left negative is treated as a hard
# I/O error and the sendagain branch is never reached.
grep -qE '\bres = 0;' <<<"$want_write_arm" || fail \
	"socket_write() no longer normalises a deferred write to 0 -- res == -1 is handled first, so the retry branch is unreachable (#451)"
want_read_arm=$(awk '/case SSL_ERROR_WANT_READ:/{c=1} c{print} c&&/break;/{exit}' <<<"$ssl_write_fn")
grep -q 'sendagain = 1' <<<"$want_read_arm" && fail \
	"socket_write() retries a WANT_READ on the write set -- that fd is always ready, so it busy-waits (#451)"

# Reset per call. A flag that is only ever set latches on: the first deferred
# write would leave every later socket looking like it still owed bytes.
grep -q 'sendagain = 0' <<<"$ssl_write_fn" || fail \
	"socket_write() never clears sendagain -- the flag latches after the first deferred write (#451)"

# The call site must consult it BEFORE the TCP_HTTP branch. Ordered after, the
# HTTP branch would claim res==0 first and advance nothing, leaving non-HTTP
# services exactly as broken as before.
call_site=$(awk '/res = socket_write\(item, outbuf, outlen\)/{c=1} c{print} c&&/TCP_HTTP/{exit}' "$SRC" | strip_comments)
[ -n "$call_site" ] || fail "contest.c no longer has the socket_write() call site (#451)"
grep -q 'sendagain' <<<"$call_site" || fail \
	"the socket_write() call site ignores sendagain -- a deferred write is still indistinguishable from nothing to send (#451)"
# The branch has to clear readpending, which is what returns the socket to
# writefds. An empty sendagain branch keeps the token and retries nothing.
sendagain_branch=$(awk '/else if \(item->sendagain\)/{c=1} c{print} c&&/^[[:space:]]*}/{exit}' <<<"$call_site")
grep -qE 'readpending[[:space:]]*=[[:space:]]*0' <<<"$sendagain_branch" || fail \
	"the sendagain branch no longer clears readpending -- the socket never goes back into writefds, so the retry never happens (#451)"

# And the socket must survive to be retried. This is the assertion that matters
# most: with the shutdown untouched, clearing readpending closes the connection
# on the same pass and the retry never comes.
shutdown_cond=$(awk '/If closed and\/or no bannergrabbing/{c=1} c{print} c&&/socket_shutdown\(item\)/{exit}' "$SRC" | strip_comments)
[ -n "$shutdown_cond" ] || fail "contest.c no longer has the post-write shutdown block (#451)"
grep -Eq '!item->readpending && !item->sendagain' <<<"$shutdown_cond" || fail \
	"contest.c closes a socket that still owes a write -- the retry can never happen (#451)"

# Hoisting socket_shutdown() into the timeout path made an old latent hazard
# reachable: SSLSETUP_PENDING is -1, so `if (item->sslrunning)` is already true
# from add_tcp_test() while ssldata is still NULL, and a connection that times
# out before setup_ssl() runs would reach SSL_shutdown(NULL). Guard on the
# objects instead. Same block selection trick as above -- the non-OpenSSL build
# has its own socket_shutdown() with nothing to free.
sd=$(awk '
	/^static void socket_shutdown/ { buf = ""; inbody = 1 }
	inbody                        { buf = buf $0 "\n" }
	inbody && /^}/                { if (buf ~ /SSL_shutdown/) { printf "%s", buf; exit } inbody = 0 }
' "$SRC" | strip_comments)
[ -n "$sd" ] || fail "contest.c has no socket_shutdown() that frees an SSL session (#451)"
grep -q 'if (item->ssldata)' <<<"$sd" || fail \
	"socket_shutdown() no longer guards on ssldata -- a timeout before setup_ssl() reaches SSL_shutdown(NULL) (#451)"
# The sslrunning guard must stay OUTSIDE it. setup_ssl()'s failure paths free
# ssldata/sslctx without clearing them and set sslrunning to 0, so a
# pointer-only guard frees dangling pointers.
grep -q 'if (item->sslrunning) {' <<<"$sd" || fail \
	"socket_shutdown() dropped the sslrunning guard -- setup_ssl()'s failure paths free without clearing, so a pointer-only test double-frees (#451)"

pass "xymonnet retries a write that SSL_write() did not accept (#451)"
