#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/server/sendmsg-empty-recipient.sh
#
# sendtomany() initialises its result to XYMONSEND_OK and only ever changes it
# inside the loop over recipients. An empty or whitespace-only recipient yields
# no token from strtok(), the loop body never runs, and the function reports a
# successful send -- with an empty response (#507). Callers cannot tell that
# from a real answer: prepare_fromnet() caches the empty response as the
# configuration, load_hostnames() never falls back to the file, and the caller
# ends up with a valid-looking, empty host list.
#
# This checks the defect where it lives: one call into lib, no daemon, no
# fixture. Its companion, tests/xymond/trimhistory-drop-needs-hostlist.sh,
# follows the same bug out to its consequence -- an emptied XYMONHISTDIR.
#
# Both on purpose. The end-to-end one states what an admin cares about and
# fails if any link from sendtomany() through load_hostnames() to trimhistory
# breaks, including links nobody has touched yet. This one cannot be blinded by
# a change anywhere outside lib/sendmsg.c, which is what an end-to-end test
# cannot promise: a guard added downstream can stop the damage, and then the
# damage is no longer evidence about the cause.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
here=$(dirname "$0")

require_cc
[ -f "$ROOT/include/config.h" ] && [ -f "$ROOT/lib/libxymoncomm.a" ] \
	|| skip "tree not built (run make first; the post-build CI suite covers this)"

work=$(mktempdir)

# The configured flags rather than a hand-rolled list: libxymon.h pulls in
# pcre2.h, which lives under /usr/local/include or /usr/pkg/include on the
# BSDs, and the link flags carry the library search path and the runtime path.
harness_cflags=$(xymon_cflags "$ROOT")
harness_ldflags=$(xymon_ldflags "$ROOT")
pcre_libs=${PCRELIBS:-$(sed -n 's/^PCRELIBS *= *//p' "$ROOT/Makefile")}
[ -n "$pcre_libs" ] || pcre_libs="-lpcre2-8"

# Archives listed twice rather than --start-group, which is GNU ld only.
# shellcheck disable=SC2086
"$CC" $harness_cflags -iquote "$ROOT/lib" -o "$work/harness" \
	"$here/sendmsg-empty-recipient-harness.c" \
	"$ROOT/lib/libxymoncomm.a" "$ROOT/lib/libxymon.a" "$ROOT/lib/libxymontime.a" \
	"$ROOT/lib/libxymoncomm.a" "$ROOT/lib/libxymon.a" \
	$pcre_libs $harness_ldflags 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; fail "harness does not compile"; }

# send RECIPIENT -- one process per case, because sendmessage() caches $XYMSRV.
send() {
	env XYMSRV="$1" XYMSERVERS="" XYMONDPORT="${2:-1984}" \
		"$work/harness" 2 2>"$work/err.log"
}

# ---- an empty recipient is nobody, not everybody ---------------------------
# XYMSRV defaults to $XYMONSERVERIP, which defaults to the XYMONHOSTIP the tree
# was built with (lib/Makefile, -DXYMONHOSTIP). A build that passed none, or an
# environment where it is empty, arrives here.
got=$(send "")
assert_not_contains "XYMONSEND_OK" "$got" \
	"an empty XYMSRV was reported as a successful send, so a caller cannot tell it from a real answer ($got)"

# ---- and neither is a recipient made only of separators --------------------
# The same hole spelled differently: strtok() on " \t" yields no token either,
# so a guard that only compares against "" would leave this one open.
got=$(send "   ")
assert_not_contains "XYMONSEND_OK" "$got" \
	"a whitespace-only XYMSRV was reported as a successful send ($got)"

# ---- control: a real recipient is still attempted --------------------------
# Without this the test is satisfied by a sendmessage() that refuses everything.
# Port 1 has nothing listening, so the send must fail -- but as a connection
# failure, having got as far as the socket, not as the bad-address refusal the
# two cases above produce.
got=$(send "127.0.0.1" 1)
assert_not_contains "XYMONSEND_OK" "$got" \
	"a refused connection was reported as a successful send ($got)"
assert_not_contains "XYMONSEND_EBADIP" "$got" \
	"a usable recipient was refused as a bad address -- the empty-recipient guard is swallowing the normal path ($got)"

pass "sending to an empty recipient is refused, and a usable one is still attempted"
