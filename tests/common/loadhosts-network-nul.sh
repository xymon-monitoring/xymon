#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/common/loadhosts-network-nul.sh
#
# A program loading hosts.cfg from xymond refuses a reply that holds a NUL,
# says so, and loads the file instead. Before, the reply was cut at the NUL
# and every host after it went missing without a word: no alerts and no
# client analysis for them, while the web pages, read from the file, still
# showed them (#716; the NUL came from an empty "optional directory", #585).
#
# Driven through the built xymongrep, by its network path,
# --loadhostsfromxymond, against tests/lib/fake-xymond.c, which serves a
# reply by its byte length. A NUL written into hosts.cfg on disk would not do:
# reading the file already stops at it, so it never reaches the reply.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)

# As tests/common/xymongrep-filter.sh: the server path first, else the
# client one; an explicit $XYMONGREP is used as it is.
default="common/xymongrep"
if [ -z "${XYMONGREP:-}" ] && [ ! -x "$ROOT/$default" ] \
		&& [ -x "$ROOT/client/xymongrep" ]; then
	default="client/xymongrep"
fi
require_bin XYMONGREP "$default"
require_cc

work=$(mktempdir)

# The NUL is at offset 31, right after the first host's line.
printf '127.0.0.1 beforehost # testtag\n\000127.0.0.2 afterhost # testtag\n' >"$work/reply"
printf '127.0.0.3 fallbackhost # testtag\n' >"$work/hosts.cfg"

"$CC" -o "$work/fake-xymond" "$ROOT/tests/lib/fake-xymond.c" 2>"$work/cc.log" \
	|| { cat "$work/cc.log" >&2; fail "fake-xymond responder does not compile"; }
"$work/fake-xymond" "$work/reply" >"$work/fake-xymond.port" &
fakepid=$!
register_cleanup "stop_process $fakepid"
for _ in 1 2 3 4 5 6 7 8 9 10; do
	[ -s "$work/fake-xymond.port" ] && break
	sleep 0.2
done
[ -s "$work/fake-xymond.port" ] || fail "fake-xymond did not report its port"

# HOSTSCFG without the "!" other tests use: this one is about the try-xymond
# path of load_hostnames() and its fall-back to the file.
rc=0
HOSTSCFG="$work/hosts.cfg" XYMONHOME="$work" XYMSRV=127.0.0.1 \
	XYMONDPORT="$(cat "$work/fake-xymond.port")" \
	"$XYMONGREP" --loadhostsfromxymond testtag >"$work/out" 2>"$work/err" || rc=$?

assert_contains "xymond returned hosts.cfg with a NUL byte at offset 31; ignoring reply" "$(cat "$work/err")" \
	"a hosts.cfg reply holding a NUL is reported (xymongrep exited $rc, printed '$(cat "$work/out")')"
assert_contains "fallbackhost" "$(cat "$work/out")" \
	"and the hosts are loaded from the file instead"
assert_not_contains "beforehost" "$(cat "$work/out")" \
	"and nothing of the refused reply is kept"
assert_not_contains "afterhost" "$(cat "$work/out")" \
	"and nothing of the refused reply is kept"

pass "a hosts.cfg reply from xymond holding a NUL is refused, reported, and the file loaded instead"
