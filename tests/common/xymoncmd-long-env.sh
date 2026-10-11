#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/common/xymoncmd-long-env.sh
#
# xymoncmd finds its configuration file whatever the length of XYMONHOME,
# and runs its command whatever the length of HOSTNAME.
#
# It looks for the file under XYMONHOME, then a few fixed places, and named
# each candidate with sprintf() into a PATH_MAX buffer; HOSTNAME went the
# same way into a 1024-byte one. A long value overflowed them, and glibc
# aborted the program: "*** buffer overflow detected ***", exit 134 (#627,
# Debian #609179). A candidate too long for the buffer now counts as missing,
# and HOSTNAME is cut to the buffer.
#
# Each candidate file here sets MARK, and `xymoncmd env` shows which one was
# read:
#   - XYMONHOME/etc/xymonserver.cfg is found and read;
#   - with a 5000-character XYMONHOME the search moves on to ./xymonserver.cfg;
#   - XYMONCLIENTHOME/etc/xymonclient.cfg is read when that variable is set;
#   - `xymoncmd true` exits 0 with a 5000-character HOSTNAME.
# The fixed places come before ./xymonserver.cfg in the search, so a host
# that has /etc/xymon/xymonserver.cfg or /etc/xymon-client/xymonclient.cfg
# cannot run the cases that need the search to get past them.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

default="common/xymoncmd"
if [ -z "${XYMONCMD:-}" ] && [ ! -x "$(find_root)/$default" ] \
		&& [ -x "$(find_root)/client/xymoncmd" ]; then
	default="client/xymoncmd"
fi
require_bin XYMONCMD "$default"

work=$(mktempdir)
long=$(printf '%5000s' '' | tr ' ' A)

# mark DIR [VAR=VALUE...] -- run `xymoncmd env` in DIR with that environment
# (XYMONCLIENTHOME and MACHINEDOTS unset), and print the MARK it loaded.
mark() {
	local dir=$1 out
	shift
	out=$(cd "$dir" && env -u XYMONCLIENTHOME -u MACHINEDOTS "$@" "$XYMONCMD" env 2>&1) \
		|| fail "xymoncmd failed in $dir: $(printf '%s\n' "$out" | tail -3)"
	printf '%s\n' "$out" | sed -n 's/^MARK=//p'
}

mkdir -p "$work/home/etc" "$work/client/etc" "$work/cwd/etc" "$work/none"
printf 'MARK=xymonhome\n' >"$work/home/etc/xymonserver.cfg"
printf 'MARK=clienthome\n' >"$work/client/etc/xymonclient.cfg"
printf 'MARK=cwd-server\n' >"$work/cwd/xymonserver.cfg"

assert_equal "xymonhome" "$(mark "$work/cwd" XYMONHOME="$work/home")" \
	"the configuration file under XYMONHOME was not the one read"

assert_equal "cwd-server" "$(mark "$work/cwd" XYMONHOME="$long")" \
	"with a 5000-character XYMONHOME, the search did not move on to ./xymonserver.cfg"

rc=0
(cd "$work/none" && env -u MACHINEDOTS HOSTNAME="$long" "$XYMONCMD" true) >"$work/host.out" 2>&1 || rc=$?
assert_equal "0" "$rc" \
	"xymoncmd did not run its command with a 5000-character HOSTNAME: $(tail -3 "$work/host.out")"

if [ -e /etc/xymon/xymonserver.cfg ] || [ -e /etc/xymon-client/xymonclient.cfg ]; then
	pass_partial "xymoncmd reads the file under XYMONHOME, moves on past one whose name is too long, and runs with a 5000-character HOSTNAME" \
		"not checked: XYMONCLIENTHOME's candidate, because this host has a system-wide configuration file the search finds first"
fi

assert_equal "clienthome" "$(mark "$work/none" XYMONHOME="$work/none" XYMONCLIENTHOME="$work/client")" \
	"the configuration file under XYMONCLIENTHOME was not the one read"

pass "xymoncmd reads the file under XYMONHOME or XYMONCLIENTHOME, moves on past a name too long for its buffer, and runs with a 5000-character HOSTNAME"
