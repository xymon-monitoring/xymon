#!/bin/sh

REL=$1
if [ "$REL" = "" ]; then
	echo "Error - missing release number"
	exit 1
fi

cd ~/hobbit
rm -rf debbuild
mkdir -p ~/hobbit/debbuild/hobbit-$REL
for f in bbdisplay bbnet bbpatches bbproxy build common docs hobbitd include lib scripts
do
        find $f/ | grep -v RCS | cpio -pdvmu ~/hobbit/debbuild/hobbit-$REL/
done
cp -p Changes configure COPYING CREDITS README ~/hobbit/debbuild/hobbit-$REL/
find ~/hobbit/debbuild/hobbit-$REL -type d|xargs chmod 755

cd debbuild
tar zcf hobbit-$REL.tar.gz hobbit-$REL

cd ~/hobbit
find debian | egrep -v "RCS|pkg" | cpio -pdvmu ~/hobbit/debbuild/hobbit-$REL/

cd debbuild/hobbit-$REL
dpkg-buildpackage -rfakeroot
mv ../hobbit_$REL-* ../../debian/pkg/

