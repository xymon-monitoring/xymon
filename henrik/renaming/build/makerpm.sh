#!/bin/sh

REL=$1
if [ "$REL" = "" ]; then
	echo "Error - missing release number"
	exit 1
fi

BASEDIR=`pwd`
cd $BASEDIR
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

cat rpm/xymon.spec | sed -e "s/@VER@/$REL/g" >rpmbuild/SPECS/xymon.spec
cp rpm/xymon-init.d rpmbuild/SOURCES/
cp rpm/xymon.logrotate rpmbuild/SOURCES/
cp rpm/xymon-client.init rpmbuild/SOURCES/
cp rpm/xymon-client.default rpmbuild/SOURCES/

mkdir -p rpmbuild/xymon-$REL
for f in bbdisplay xymonnet bbpatches bbproxy build common contrib docs xymond web include lib client demotool
do
        find $f/ | egrep -v "RCS|.svn" | cpio -pdvmu $BASEDIR/rpmbuild/xymon-$REL/
done
cp -p Changes configure configure.server configure.client COPYING CREDITS README README.CLIENT RELEASENOTES $BASEDIR/rpmbuild/xymon-$REL/
find $BASEDIR/rpmbuild/xymon-$REL -type d|xargs chmod 755

cd rpmbuild
#pushd xymon-$REL
#make -f $HOME/xymon/Makefile.home distclean
#popd
tar zcf SOURCES/xymon-$REL.tar.gz xymon-$REL
rm -rf xymon-$REL
HOME=`pwd` rpmbuild -ba --clean SPECS/xymon.spec
#rpm --addsign RPMS/i?86/xymon-$REL-*.i?86.rpm RPMS/i386/xymon-client-$REL-*.i?86.rpm SRPMS/xymon-$REL-*.src.rpm
# mv RPMS/i?86/xymon-$REL-*.i?86.rpm RPMS/i?86/xymon-client-$REL-*.i?86.rpm SRPMS/xymon-$REL-*.src.rpm ../rpm/pkg/

