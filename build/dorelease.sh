#!/bin/sh
set -eu

VERSION="${1:-}"
if [ "$VERSION" = "" ]
then
	echo "$0 VERSION"
	exit 1
fi

./build/generate-md5.sh >build/md5.dat.new
mv build/md5.dat.new build/md5.dat

# include/version.h is the version the binaries report: xymond's own status,
# XYMONDREL on the web pages, the locator's PONG. Nothing else sets it, and a
# line that stops matching must fail the prep rather than ship the old one.
sed -e "s/^#define VERSION \".*\"\$/#define VERSION \"$VERSION\"/" include/version.h >include/version.h.new
mv include/version.h.new include/version.h
grep -qxF "#define VERSION \"$VERSION\"" include/version.h || {
	echo "$0: no '#define VERSION' line to stamp in include/version.h" >&2
	exit 1
}

./build/updmanver "$VERSION"
./build/makehtml.sh --version "$VERSION"

