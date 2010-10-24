#!/bin/bash

REL=$1
if [ "$REL" = "" ]; then
	echo "Error - missing release number"
	exit 1
fi

BASEDIR=`pwd`
cd $BASEDIR

rm -rf debbuild
mkdir -p $BASEDIR/debbuild/xymon-$REL
for f in bbdisplay bbnet bbpatches bbproxy build common contrib docs hobbitd web include lib client demotool
do
        find $f/ | egrep -v "RCS|\.svn" | cpio -pdvmu $BASEDIR/debbuild/xymon-$REL/
done
cp -p Changes configure configure.server configure.client COPYING CREDITS README README.CLIENT RELEASENOTES $BASEDIR/debbuild/xymon-$REL/
find $BASEDIR/debbuild/xymon-$REL -type d|xargs chmod 755

cd debbuild
pushd xymon-$REL
make -f ../../Makefile distclean
popd
tar zcf xymon-$REL.tar.gz xymon-$REL

cd $BASEDIR
find debian | egrep -v "RCS|pkg|\.svn" | cpio -pdvmu $BASEDIR/debbuild/xymon-$REL/

cd debbuild/xymon-$REL
dpkg-buildpackage -rfakeroot -kA6EDAB79
#mv ../xymon*$REL*.{deb,dsc,changes} ../../debian/pkg/

