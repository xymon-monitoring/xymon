#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/libxymon/sendmsg-empty-recipient.sh
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
# fixture, so no guard added downstream can hide it.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
. "$(dirname "$0")/../lib/build-harness.sh"

ROOT=$(find_root)
here=$(dirname "$0")

# The client archives, which every build makes: sendmsg.o is in
# libxymonclientcomm.a as well as in the server's libxymoncomm.a.
require_c_buildenv "$ROOT"
[ -f "$(xymon_lib "$ROOT" libxymonclient.a)" ] \
	|| skip "tree not built (run make first; the post-build CI suite covers this)"

work=$(mktempdir)
build_xymon_libs "$ROOT" "$work/libbuild.log" libxymonclientcomm.a libxymonclient.a
build_harness "$work" "$work/harness" \
	"$here/sendmsg-empty-recipient-harness.c" libxymonclientcomm.a libxymonclient.a

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
