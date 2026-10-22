#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/client/clientupdate-relocated.sh
#
# A client installed with INSTALLCLIENTBINDIR or INSTALLCLIENTETCDIR has its
# bin or etc relocated, and a link left in its place under XYMONHOME.
# clientupdate unpacks an update archive there with "tar xf -", and GNU tar,
# like the native tar of OpenBSD and FreeBSD, replaces such a link with the
# directory the archive names: the update would land beside the installed
# files and leave them stale. clientupdate refuses instead, says why, and
# leaves the link alone.
#
# The refusal comes before any download, so no server is needed: the check
# is that the message is given and the link survives. Without the refusal,
# clientupdate would go on to tar and the download, and fail without it.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin CLIENTUPDATE client/clientupdate

work=$(mktempdir)

# One case per relocated directory: the link stands where XYMONHOME/<dir> was.
for d in bin etc; do
	home="$work/$d/home"
	mkdir -p "$home/tmp" "$work/$d/elsewhere"
	ln -s "$work/$d/elsewhere" "$home/$d"
	rc=0
	out=$(env XYMONHOME="$home" XYMONTMP="$home/tmp" XYMSRV=127.0.0.1 XYMONDPORT=1 \
		"$CLIENTUPDATE" --update=test.v2 2>&1) || rc=$?
	[ "$rc" -ne 0 ] || fail "clientupdate reported success updating a client whose $d is relocated"
	grep -q "$home/$d is a link to a relocated directory" <<<"$out" \
		|| fail "clientupdate did not refuse a client whose $d is relocated: $(sed -n '1,5p' <<<"$out")"
	[ -L "$home/$d" ] || fail "clientupdate replaced the $d link with a directory"
done

pass "clientupdate refuses to update a client whose bin or etc is relocated, and leaves the link in place"
