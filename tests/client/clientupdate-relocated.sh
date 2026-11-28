#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/client/clientupdate-relocated.sh
#
# A client installed with INSTALLCLIENT{BIN,ETC,TMP,LOG,EXT}DIR has its
# bin, etc, tmp, logs or ext relocated, and a link left in its place under
# XYMONHOME; a packaging such as Debian's leaves the same links.
# clientupdate unpacks an update archive there with "tar xf -", and GNU tar,
# like the native tar of OpenBSD and FreeBSD, replaces such a link with the
# directory the archive names: the update would land beside the installed
# files and leave them stale. clientupdate refuses instead, says why, and
# leaves the link alone.
#
# The refusal comes before any download, so no server is needed: the check
# is that the message is given, and that --reexec takes no lock before it.
# Without the refusal, clientupdate would go on to tar and the download, and
# fail without it. That the link survives is checked too, but with no server
# tar never runs, so it cannot catch tar replacing the link.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

require_bin CLIENTUPDATE client/clientupdate

work=$(mktempdir)

# One case per relocated directory: the link stands where XYMONHOME/<dir> was.
for d in bin etc tmp logs ext; do
	home="$work/$d/home"
	mkdir -p "$home" "$work/$d/elsewhere"
	[ "$d" = tmp ] || mkdir "$home/tmp"  # the tmp case links tmp itself
	ln -s "$work/$d/elsewhere" "$home/$d"
	rc=0
	out=$(env XYMONHOME="$home" XYMONTMP="$home/tmp" XYMSRV=127.0.0.1 XYMONDPORT=1 \
		"$CLIENTUPDATE" --update=test.v2 2>&1) || rc=$?
	[ "$rc" -ne 0 ] || fail "clientupdate reported success updating a client whose $d is relocated"
	grep -q "$home/$d is a link to a relocated directory" <<<"$out" \
		|| fail "clientupdate did not refuse a client whose $d is relocated: $(sed -n '1,5p' <<<"$out")"
	[ -L "$home/$d" ] || fail "clientupdate replaced the $d link with a directory"

	# --reexec, which xymonclient.sh runs on every mismatched cycle, refuses
	# before it takes the lock and copies itself into XYMONTMP. XYMONTMP does
	# not exist here, so a copy attempted first fails with another message.
	rc=0
	out=$(env XYMONHOME="$home" XYMONTMP="$home/notmp" XYMSRV=127.0.0.1 XYMONDPORT=1 \
		"$CLIENTUPDATE" --update=test.v2 --reexec 2>&1) || rc=$?
	[ "$rc" -ne 0 ] || fail "clientupdate --reexec reported success on a client whose $d is relocated"
	grep -q "$home/$d is a link to a relocated directory" <<<"$out" \
		|| fail "clientupdate --reexec did not refuse before copying itself: $(sed -n '1,5p' <<<"$out")"
	[ ! -e "$home/tmp/.inprogress.update" ] \
		|| fail "clientupdate --reexec took the update lock before refusing"
done

pass "clientupdate refuses to update a client whose bin, etc, tmp, logs or ext is relocated, before --reexec copies itself, and leaves the link in place"
