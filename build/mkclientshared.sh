#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
#
# build/mkclientshared.sh -- copy each client/shared/<name>.sh into the
# "BEGIN SHARED <name>" region of every client script that has one.
#
# The client scripts cannot source a shared file: xymonclient.sh picks one by
# uname and *executes* it, and during a clientupdate there is a window where a
# sourced fragment is not there yet, which aborts the client on exactly the
# hosts the code protects. So the copies stay -- but they are written here
# once and stamped out, instead of being edited five times and compared
# afterwards by a test.
#
# Output is committed, so nothing about the build or the installed client
# changes: each xymonclient-<os>.sh remains one self-contained script. Only the
# marked regions are machine-owned; everything else in those files is still
# edited by hand.
#
# Run with no arguments to regenerate every client. CI runs it and fails if the
# tree moves, the same way it does for docs/manpages.

set -eu

cd "$(dirname "$0")/.."

status=0
for frag in client/shared/*.sh; do
	name=$(basename "$frag" .sh)
	found=0
	for client in client/xymonclient-*.sh; do
		grep -q "^# BEGIN SHARED $name " "$client" || continue
		grep -q "^# END SHARED $name\$" "$client" || {
			echo "$client: BEGIN SHARED $name without a matching END" >&2
			status=1
			continue
		}
		found=$((found + 1))
		awk -v name="$name" -v frag="$frag" '
			index($0, "# BEGIN SHARED " name " ") == 1 {
				print
				while ((getline line < frag) > 0) print line
				close(frag)
				skip = 1
				next
			}
			skip && $0 == "# END SHARED " name { skip = 0; print; next }
			skip { next }
			{ print }
		' "$client" > "$client.new"
		# Keep the mode: these are executable scripts.
		cat "$client.new" > "$client"
		rm -f "$client.new"
	done
	[ "$found" -gt 0 ] || { echo "$frag: no client declares a region for it" >&2; status=1; }
	echo "$name -> $found client(s)"
done
exit $status
