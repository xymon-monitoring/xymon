#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/notify-channel-buflen.sh
#
# Regression guard for the heap buffer overflow in handle_notify()
# (xymond/xymond.c) -- issue #528.
#
# handle_notify() allocated `1024 + strlen(msgtext)` and then sprintf'ed four
# strings into it: the hostname, the testname, the host's full comma-separated
# pagepath list and the message text. The fixed 1024 had to cover the first
# three, and none of them is bounded -- XMH_ALLPAGEPATHS carries a complete path
# for every page the host is listed on -- so a host on enough pages wrote past
# the allocation.
#
# The fix does not correct the arithmetic, it removes it: the message is built
# with addtobuffer_many() into a strbuffer. Adding up the fields and counting
# the separators in a format string by hand is the step that was wrong, and a
# corrected hand-count is still a hand-count.
#
# A static check, like the first half of tests/server/combostatus-overflow.sh
# and for the same reason: driving the real code needs a live xymond, and a heap
# overflow only *shows* there if the allocator happens to trap it. The write
# lands before the message is posted, so on an allocator that lets it pass the
# message still reaches the channel intact and a behavioural assertion goes
# green on buggy code -- which is what an earlier version of this test did.
#
# Unlike combostatus-overflow.sh there is no second, dynamic half, because there
# would be nothing of this change in it. What the fixed form relies on is that a
# strbuffer grows to fit, and that is lib/strfunc.c's behaviour rather than
# handle_notify()'s: a demo of it passes unchanged for every possible edit to
# the function under test.
#
# Everything is matched inside handle_notify()'s body. handle_data() a few
# hundred lines above carries the same shape, so a file-wide match is satisfied
# by the wrong function and the guard passes while handle_notify() regresses.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
SRC="$ROOT/xymond/xymond.c"

[ -f "$SRC" ] || skip "xymond/xymond.c absent"

fn=$(sed -n '/^void handle_notify(/,/^}/p' "$SRC")
[ -n "$fn" ] || fail "cannot find handle_notify() in $SRC (#528)"

# The whole call on one line, whitespace removed, so the four fields and their
# separators can be matched as a single ordered pattern. Substring matches taken
# one field at a time say nothing about order, and a swapped hostname and
# testname misroutes every notify alert.
call=$(printf '%s\n' "$fn" | sed -n '/addtobuffer_many(channelmsg,/,/NULL);/p' | tr -d ' \t\n')
[ -n "$call" ] \
	|| fail "handle_notify() no longer builds the page-channel message with addtobuffer_many (#528)"

# Fully adjacent: no wildcard between the fields, so an extra field inserted
# into the middle of the wire format is caught as well as a reordering. Every
# downstream parser of the page channel reads these positionally.
case $call in
	*'(hostname?hostname:""),"|",(testname?testname:""),"|",(pagepath?pagepath:""),"\n",(msgtext?msgtext:""),NULL'*) ;;
	*) fail "handle_notify() no longer writes hostname|testname|pagepaths<newline>text, those four and in that order (#528): $call" ;;
esac

# Building the message is not the whole of it. Each of these was verified to
# regress green while only the construction was checked: posting something other
# than the buffer sends the raw command line to the pagers, dropping the free
# leaks the buffer on every notify, and dropping the NULL check restores the
# unchecked dereference this change exists to remove.
case $fn in
	*'posttochannel(pagechn, "notify", msg, sender, hostname, NULL, STRBUF(channelmsg));'*) ;;
	*) fail "handle_notify() no longer posts the buffer it built to the page channel (#528)" ;;
esac
case $fn in
	*'freestrbuffer(channelmsg);'*) ;;
	*) fail "handle_notify() no longer frees the page-channel buffer (#528)" ;;
esac
case $fn in
	*'if (channelmsg == NULL) {'*) ;;
	*) fail "handle_notify() no longer checks the allocation before using it (#528)" ;;
esac

# The shapes that carried the defect: a buffer sized by hand -- whichever
# allocator, or a fixed array on the stack -- and a format written into it.
# Either is needed to reintroduce the overflow.
#
# Comment lines are dropped first. These patterns describe the old code, so the
# comment that explains what the old code did would otherwise match it, and so
# would any unrelated fixed-size local a future edit adds.
code=$(printf '%s\n' "$fn" | grep -v '^[[:space:]]*[*/]')

printf '%s\n' "$code" | grep -Eq '[a-z]alloc\(|\[[0-9]+\]' \
	&& fail "handle_notify() sizes a page-channel buffer by hand again (#528)"

# sprintf matched as NOT preceded by an 'n', so snprintf does not trip it.
printf '%s\n' "$code" | grep -Eq '(^|[^n])sprintf\(' \
	&& fail "handle_notify() formats the page-channel message into a fixed buffer again (#528)"

pass "handle_notify() builds the page-channel message in a strbuffer, fields in order (#528)"
