#!/bin/sh

REL=$1
if [ "$REL" = "" ]; then
	echo "Error - missing release number"
	exit 1
fi

cd ~/hobbit
rm -rf rpmbuild

# Setup a temp. rpm build directory.
mkdir -p rpmbuild/RPMS rpmbuild/RPMS/i386 rpmbuild/BUILD rpmbuild/SPECS rpmbuild/SRPMS rpmbuild/SOURCES
cat >rpmbuild/.rpmmacros <<EOF1
# Default macros for my enviroment.
%_topdir `pwd`/rpmbuild
%_tmppath /tmp
EOF1
cat >rpmbuild/.rpmrc <<EOF2
# rpmrc
buildarchtranslate: i386: i386
buildarchtranslate: i486: i386
buildarchtranslate: i586: i386
buildarchtranslate: i686: i386
EOF2

cp rpm/hobbit.spec rpmbuild/SPECS/
cp rpm/hobbit-init.d rpmbuild/SOURCES/
cp rpm/hobbit.logrotate rpmbuild/SOURCES/

mkdir -p rpmbuild/hobbit-$REL
for f in bbdisplay bbnet bbpatches bbproxy build common docs hobbitd include lib scripts
do
        find $f/ | grep -v RCS | cpio -pdvmu ~/hobbit/rpmbuild/hobbit-$REL/
done
cp -p Changes configure COPYING CREDITS README ~/hobbit/rpmbuild/hobbit-$REL/
find ~/hobbit/rpmbuild/hobbit-$REL -type d|xargs chmod 755

cd rpmbuild
tar zcf SOURCES/hobbit-$REL.tar.gz hobbit-$REL
rm -rf hobbit-$REL
HOME=`pwd` rpmbuild -ba --clean SPECS/hobbit.spec

