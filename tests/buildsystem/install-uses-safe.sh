#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/buildsystem/install-uses-safe.sh
#
# The install writes into directories that end up owned by the unprivileged
# Xymon user, where a planted symlink at a destination name would be followed.
# Every write into $(INSTALLROOT) therefore goes through a symlink-safe writer --
# build/install-safe or setup-newfiles -- and never a raw cp/mkdir/chown/chgrp/
# chmod, nor a shell redirect (>) straight onto a destination, which would follow
# a symlink there first. A find ... -type f -exec, which cannot select a symlink,
# and ln, which creates a name rather than writing through one, are allowed; so
# are the merge-sects/merge-lines mergers, symlink-safe and kept for a future
# config merge though the install no longer uses them. This reads the install
# makefiles statically, so it runs on any platform and needs no build.
#
# It replaces the earlier guard that checked these commands carried -h / a
# preceding rm -f: with install-safe doing the placement, the commands are gone,
# not guarded.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)

# Every makefile that installs: the server skeleton, the client, and each
# component that copies programs, extensions, manpages or config into a
# directory $(XYMONUSER) ends up owning.
MAKEFILES="build/Makefile.rules client/Makefile common/Makefile xymond/Makefile xymonnet/Makefile web/Makefile xymongen/Makefile xymonproxy/Makefile docs/Makefile lib/Makefile"

# A raw command word that must not act on an install destination directly.
RAW='(^|[^[:alnum:]_])(cp|mkdir|chown|chgrp|chmod)[[:space:]]'
# A shell redirect straight onto a destination follows a symlink planted there
# before any safe writer sees the name.
REDIR='>[[:space:]]*\$\(INSTALLROOT\)'

bad=""
for f in $MAKEFILES; do
	[ -f "$ROOT/$f" ] || continue
	# Lines that touch an install destination, are not a safe-writer invocation,
	# and still run a raw command or redirect onto it.
	hits=$(sed 's/#.*//' "$ROOT/$f" \
		| grep -nE '\$\(INSTALLROOT\)' \
		| grep -vE 'INSTALLSAFE|install-safe|merge-sects|merge-lines|setup-newfiles' \
		| grep -vE 'find[^|]*-type f' \
		| grep -E "$RAW|$REDIR" \
		| grep . || true)
	[ -z "$hits" ] || bad="$bad
$f:
$hits"
done

[ -z "$bad" ] || fail "an install makefile writes into \$(INSTALLROOT) with a raw command or a > redirect instead of install-safe/setup-newfiles:$bad"

pass "every install into \$(INSTALLROOT) goes through install-safe or setup-newfiles, no raw write or redirect"
