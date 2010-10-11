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

cat rpm/hobbit.spec | sed -e "s/@VER@/$REL/g" >rpmbuild/SPECS/hobbit.spec
cp rpm/hobbit-init.d rpmbuild/SOURCES/
cp rpm/hobbit.logrotate rpmbuild/SOURCES/
cp rpm/hobbit-client.init rpmbuild/SOURCES/
cp rpm/hobbit-client.default rpmbuild/SOURCES/

mkdir -p rpmbuild/hobbit-$REL
for f in bbdisplay bbnet bbpatches bbproxy build common contrib docs hobbitd web include lib client demotool
do
        find $f/ | grep -v RCS | cpio -pdvmu $BASEDIR/rpmbuild/hobbit-$REL/
done
cp -p Changes configure configure.server configure.client COPYING CREDITS README README.CLIENT RELEASENOTES $BASEDIR/rpmbuild/hobbit-$REL/
find $BASEDIR/rpmbuild/hobbit-$REL -type d|xargs chmod 755

cd rpmbuild
pushd hobbit-$REL
make -f $HOME/hobbit/Makefile.home distclean
popd
tar zcf SOURCES/hobbit-$REL.tar.gz hobbit-$REL
rm -rf hobbit-$REL
HOME=`pwd` rpmbuild -ba --clean SPECS/hobbit.spec
rpm --addsign RPMS/i?86/hobbit-$REL-*.i?86.rpm RPMS/i386/hobbit-client-$REL-*.i?86.rpm SRPMS/hobbit-$REL-*.src.rpm
mv RPMS/i?86/hobbit-$REL-*.i?86.rpm RPMS/i?86/hobbit-client-$REL-*.i?86.rpm SRPMS/hobbit-$REL-*.src.rpm ../rpm/pkg/

