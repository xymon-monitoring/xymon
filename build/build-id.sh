#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Write build/build-id: the commit a build that is not a release comes from,
# as git describe names it after the last release tag ("4.3.31-159-g2dcad5db7",
# "-dirty" when the tree has changes). Empty for a release: a clean checkout
# of a rel-* tag, or a tree that is not a git checkout of its own, such as a
# release tarball. XYMON_BUILD_ID, when set, is used as given, even empty.
#
# The file is replaced only when its content changes, so an unchanged
# identity rebuilds nothing. Only the build runs this, never an install.

cd "$(dirname "$0")/.." || exit 1

if [ -n "${XYMON_BUILD_ID+set}" ]; then
	id=$XYMON_BUILD_ID
elif [ "$(git rev-parse --show-toplevel 2>/dev/null)" = "$(pwd -P)" ]; then
	id=$(git describe --tags --always --dirty --match 'rel-*' 2>/dev/null)
	# A clean checkout of the tag is that release, already named by its number
	[ "$id" = "$(git describe --tags --abbrev=0 --match 'rel-*' 2>/dev/null)" ] && id=
	id=${id#rel-}
else
	id=
fi

# It becomes a C string: keep to the characters git describe produces
printf '%s\n' "$id" | tr -cd 'A-Za-z0-9._+\n-' >build/build-id.new
if cmp -s build/build-id.new build/build-id; then
	rm -f build/build-id.new
else
	mv build/build-id.new build/build-id
fi
