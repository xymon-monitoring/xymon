#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/client/logfetch-noexec-default.sh
#
# The shipped client runs logfetch with --noexec, so a command in backticks
# from the server's client-local.cfg is not run on the client (#625, Debian
# #996778). Such a command comes with the configuration the server hands
# out, so without the option whoever could change that file ran commands on
# every client, as the client's user.
#
# Checked: LOGFETCHOPTS as the generated client/xymonclient.cfg sets it,
# given to logfetch with a "log:" entry whose file name is a command that
# would leave a marker file: the marker is not made. And, so the check
# cannot pass for the wrong reason, the same entry without the option does
# make it.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin LOGFETCH client/logfetch
require_cfg XYMONCLIENT_CFG client/xymonclient.cfg

work=$(mktempdir)
printf 'log:`touch %s/ran; echo /dev/null`:1024\n' "$work" >"$work/logfetch.cfg"

# The value the client passes, as xymonclient.sh gets it from the file.
opts=$(sh -c '. "$1" >/dev/null 2>&1; printf "%s" "$LOGFETCHOPTS"' sh "$XYMONCLIENT_CFG")

# shellcheck disable=SC2086  # LOGFETCHOPTS is a list of options, as in xymonclient.sh
"$LOGFETCH" $opts "$work/logfetch.cfg" "$work/status" >/dev/null 2>"$work/err.default" \
	|| fail "logfetch failed with LOGFETCHOPTS='$opts': $(cat "$work/err.default")"
[ -e "$work/ran" ] \
	&& fail "with the shipped LOGFETCHOPTS ('$opts'), logfetch ran a command from client-local.cfg"

"$LOGFETCH" "$work/logfetch.cfg" "$work/status2" >/dev/null 2>"$work/err.plain" \
	|| fail "logfetch failed without options: $(cat "$work/err.plain")"
[ -e "$work/ran" ] \
	|| fail "without --noexec, logfetch did not run the command either: this test no longer tells the two apart"

pass "the shipped LOGFETCHOPTS ('$opts') keeps logfetch from running a command from client-local.cfg, which it runs without it"
