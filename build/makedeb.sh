#!/bin/bash

REL=$1
if [ "$REL" = "" ]; then
	echo "Error - missing release number"
	exit 1
fi

BASEDIR=`pwd`
cd $BASEDIR

rm -rf debbuild
mkdir -p $BASEDIR/debbuild/hobbit-$REL
for f in bbdisplay bbnet bbpatches bbproxy build common contrib docs hobbitd web include lib client demotool
do
        find $f/ | egrep -v "RCS|\.svn" | cpio -pdvmu $BASEDIR/debbuild/hobbit-$REL/
done
cp -p Changes configure configure.server configure.client COPYING CREDITS README README.CLIENT RELEASENOTES $BASEDIR/debbuild/hobbit-$REL/
find $BASEDIR/debbuild/hobbit-$REL -type d|xargs chmod 755

cd debbuild
pushd hobbit-$REL
make -f $HOME/hobbit/Makefile.home distclean
popd
tar zcf hobbit-$REL.tar.gz hobbit-$REL

cd $BASEDIR
find debian | egrep -v "RCS|pkg|\.svn" | cpio -pdvmu $BASEDIR/debbuild/hobbit-$REL/

cd debbuild/hobbit-$REL
dpkg-buildpackage -rfakeroot -kA6EDAB79
mv ../hobbit*$REL*.{deb,dsc,changes} ../../debian/pkg/

