#!/bin/sh

VERSION="$1"
if [ "$VERSION" = "" ]
then
	echo "$0 VERSION"
	exit 1
fi

./build/generate-md5.sh >build/md5.dat.new
mv build/md5.dat.new build/md5.dat

./build/updmanver $VERSION
./build/makehtml.sh $VERSION

exit 0

