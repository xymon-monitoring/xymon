#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/network/tls-handshake-wait.sh
#
# Guard for xymon-monitoring/xymon#452: while a TLS handshake is pending,
# xymonnet must wait for the direction SSL_connect() actually asked for.
#
# do_tcp_tests() registers each socket with select() for read OR write, never
# both. Mid-handshake readpending is still 0 -- do_talk stays false until the
# handshake completes -- so the plain rule asks for writability while
# SSL_connect() is blocked on a read. A connected socket is almost always
# writable, so select() returns immediately, every iteration, until the peer
# finally answers: a busy-wait for the length of one handshake round trip,
# multiplied by --concurrency.
#
# Measured on a peer that accepts and then never speaks, 8s timeout, same tree
# and container either way: 897 pselect6 / 152880 read before, 20 / 488 after.
#
# A behavioural run needs a stalled TLS peer plus syscall counting, which this
# suite has no vocabulary for and which would be timing-dependent; the build CI
# already compiles these files, so this is a static guard that the wiring
# survives future edits -- the same trade tls13-support.sh makes. Skips only
# when the sources are absent (an autopkgtest run with no tree); a present tree
# that has lost the wiring is a regression and fails.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
SRC="$ROOT/xymonnet/contest.c"
HDR="$ROOT/xymonnet/contest.h"

for f in "$SRC" "$HDR"; do
	[ -f "$f" ] || skip "$(basename "$f") absent"
done

# grep, not assert_contains: the latter echoes the whole haystack on failure,
# and dumping 200 lines of header buries the one line that matters.
grep -Eq '^[[:space:]]*int[[:space:]]+sslwantwrite;' "$HDR" || fail \
	"contest.h lost the sslwantwrite flag -- the handshake cannot record its direction (#452)"

# The flag is only meaningful if it is SET from the WANT_READ/WANT_WRITE arm of
# SSL_connect()'s error switch. A whole-file grep is a false green: the
# assignment could survive anywhere, including in dead code. Extract just that
# arm -- from the case labels up to and including its break -- and assert the
# assignment lives INSIDE it.
# Strip comments here too: this arm is commented with the identifiers being
# asserted, so prose could satisfy a claim about the code beside it.
want_arm=$(awk '/case SSL_ERROR_WANT_READ:/{c=1} c{print} c&&/break;/{exit}' "$SRC" \
	| sed -e 's,/\*.*\*/,,' -e '/^[[:space:]]*\/\*/d' -e '/^[[:space:]]*\*/d')
[ -n "$want_arm" ] || fail \
	"contest.c no longer has the SSL_connect WANT_READ/WANT_WRITE arm (#452)"
# Assert the MAPPING, not the token. `sslwantwrite = 0` or a comparison
# against WANT_READ would both keep the word while inverting or flattening the
# meaning, and either sends select() the wrong way.
grep -Eq 'sslwantwrite[[:space:]]*=[[:space:]]*\(?[A-Za-z_]+[[:space:]]*==[[:space:]]*SSL_ERROR_WANT_WRITE' <<<"$want_arm" || fail \
	"contest.c no longer derives the handshake direction from an == SSL_ERROR_WANT_WRITE test -- a constant or an inverted comparison sends select() the wrong way (#452)"

# The registration itself. Two ways to lose the fix while keeping the token:
# dropping the SSLSETUP_PENDING branch (falls back to the writability rule and
# spins again), or ordering it AFTER the readpending branch (unreachable,
# because readpending is 0 exactly when this matters). Extract the whole
# if/else-if chain and assert both the presence and the order.
reg=$(awk '/FD_ZERO\(&readfds\)/{c=1} c{print} c&&/item->fd > maxfd/{exit}' "$SRC")
[ -n "$reg" ] || fail "contest.c no longer has the select() fd-registration block (#452)"

# Assert against code, not prose. The block is commented, and those comments
# name the very identifiers being asserted -- so grepping the raw text lets a
# comment satisfy an assertion about the condition beside it. Deleting the
# item->open gate while leaving the comment that explains it is exactly the
# plausible edit, and it passed until this strip was added.
reg=$(sed -e 's,/\*.*\*/,,' -e '/^[[:space:]]*\/\*/d' -e '/^[[:space:]]*\*/d' <<<"$reg")

grep -q 'SSLSETUP_PENDING' <<<"$reg" || fail \
	"contest.c no longer special-cases a pending handshake when registering the socket -- it will busy-wait (#452)"
grep -Eq 'sslwantwrite[[:space:]]*\?[[:space:]]*&writefds[[:space:]]*:[[:space:]]*&readfds' <<<"$reg" || fail \
	"contest.c no longer maps sslwantwrite to writefds and its negation to readfds -- an inverted ternary keeps the token and waits the wrong way (#452)"

pending_at=$(grep -n 'SSLSETUP_PENDING' <<<"$reg" | head -1 | cut -d: -f1)
readpending_at=$(grep -n 'item->readpending' <<<"$reg" | head -1 | cut -d: -f1)
[ -n "$pending_at" ] && [ -n "$readpending_at" ] || fail \
	"contest.c fd-registration block no longer contains both branches (#452)"
[ "$pending_at" -lt "$readpending_at" ] || fail \
	"contest.c tests readpending before the pending-handshake case -- the handshake branch is unreachable, since readpending is 0 precisely while the handshake runs (#452)"

# Gated on item->open: before the connection completes, writability is still how
# completion is detected, so an ungated branch would wait to read a ServerHello
# from a socket that has not finished connecting.
grep -Eq 'if \(item->open &&.*SSLSETUP_PENDING' <<<"$reg" || fail \
	"contest.c no longer gates the pending-handshake branch on item->open -- connect() completion is detected by writability (#452)"

# Registering by direction makes the read branch the normal path for a pending
# handshake, and that branch used to `break` out of the loop over items -- which
# would skip every socket after this one in the same pass. Harmless while the
# branch was unreachable mid-handshake; not harmless now.
# Anchored on the construct, not on one spelling of it: this arm is a single
# line today and may become a block, and a guard that only recognises the
# current layout stops guarding without saying so.
read_arm=$(awk '/if \(item->sslrunning == SSLSETUP_PENDING\)/{c=1} c{print} c&&/res = socket_read\(/{exit}' "$SRC")
[ -n "$read_arm" ] || fail \
	"contest.c no longer has the pending-handshake arm of the read branch (#452)"
grep -qE '\bbreak;' <<<"$read_arm" && fail \
	"contest.c breaks out of the item loop while a handshake is pending -- every socket after this one is skipped for the pass (#452)"

# Registering pending handshakes for readability means a handshake can now
# COMPLETE in the read arm, where the write arm has not run: sendtxt unsent,
# readpending unset, silenttest not honoured. Falling through into the read
# there is the regression, and forbidding `break` alone does not catch it.
grep -q 'if (!item->readpending)' <<<"$read_arm" || fail \
	"contest.c reads immediately after a handshake completes in the read arm -- the write arm never ran, so the send is skipped and a silenttest still collects a banner (#452)"

pass "xymonnet waits for the direction SSL_connect asked for (#452)"
