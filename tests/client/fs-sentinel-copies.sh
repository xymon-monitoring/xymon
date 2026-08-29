#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/client/fs-sentinel-copies.sh
#
# The remote-df sentinel (#316) is duplicated, not shared: the client scripts
# source nothing -- xymonclient.sh picks one by uname and *executes* it -- and a
# sourced fragment would leave a window during clientupdate where the fragment
# is missing and the whole client aborts, purpling exactly the hosts the
# sentinel protects. Copies it is, then.
#
# What makes that safe is this test rather than discipline. The df/inode filter
# was ported the same way and drifted within a release: multi-token
# INCLUDE_TYPES tested on one client, the empty-list marker on another, nobody
# deciding either. So: every copy of df_sentinel() must be byte-identical, and
# a port that "improves" one copy in passing has to improve all of them or say
# why in a commit that changes this test.
#
# Only df_sentinel() is compared. Finding the mounts that need it is the
# documented per-OS seam -- /proc/mounts on Linux, mount(8) on the BSDs and
# macOS -- and those bodies differ on purpose.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
TMP=$(mktempdir)

# Normalise the one thing a copy may legitimately differ in: the client's own
# name in a message.
extract() {
	sed -n '/^df_sentinel()/,/^}/p' "$1" | sed -e 's/xymonclient[-a-z_0-9]*:/xymonclient:/g'
}

copies=0
reference=
for script in "$ROOT"/client/xymonclient-*.sh; do
	grep -q '^df_sentinel()' "$script" || continue
	os=$(basename "$script" .sh)
	extract "$script" > "$TMP/$os"
	[ -s "$TMP/$os" ] || fail "$os: df_sentinel() is declared but its body could not be extracted -- the '}' terminator moved?"
	copies=$((copies + 1))
	if [ -z "$reference" ]; then
		reference=$os
		continue
	fi
	if ! diff -u "$TMP/$reference" "$TMP/$os" > "$TMP/diff.$os"; then
		printf '%s\n' "$(cat "$TMP/diff.$os")" >&2
		fail "$os's df_sentinel() has drifted from $reference's -- see the diff above; the sentinel is duplicated and must stay identical"
	fi
done

# Zero copies is not "nothing to compare", it is the sentinel having vanished.
[ "$copies" -gt 0 ] || fail "no client declares df_sentinel(): the remote-df sentinel regressed out of the tree (#316)"

pass "the remote-df sentinel is identical in all $copies client(s) that carry it"
