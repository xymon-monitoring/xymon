#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
#
# build/bump-dev.sh X.Y.Z -- after the X.Y.Z release, set include/version.h
# to X.Y.(Z+1)-dev, so a build from main names what it is becoming rather
# than the release (RELEASING.md, "After the release"; #676). It changes
# that one line and nothing else: the manual pages keep the release's
# version until the next release stamps them.
#
# Run from the top of the tree. When version.h no longer names X.Y.Z, main
# has moved on already -- bumped by hand, or past another release -- and the
# file is left alone. Prints the version.h line it leaves.

set -eu

VERSION="${1:-}"
case "$VERSION" in
	*[!0-9.]* | .* | *. | *..* | "")
		echo "usage: $0 X.Y.Z (the version just released)" >&2; exit 1 ;;
esac
IFS=. read -r major minor patch rest <<EOT
$VERSION
EOT
if [ -z "$major" ] || [ -z "$minor" ] || [ -z "$patch" ] || [ -n "$rest" ]; then
	echo "usage: $0 X.Y.Z (the version just released)" >&2; exit 1
fi

next="$major.$minor.$((patch + 1))-dev"
released="#define VERSION \"$VERSION\""

if ! grep -q '^#define VERSION "' include/version.h; then
	echo "$0: no '#define VERSION' line in include/version.h" >&2
	exit 1
fi
if grep -qxF "$released" include/version.h; then
	sed -e "s/^#define VERSION \".*\"\$/#define VERSION \"$next\"/" include/version.h >include/version.h.new
	mv include/version.h.new include/version.h
fi
grep '^#define VERSION "' include/version.h
