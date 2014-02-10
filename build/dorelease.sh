#!/bin/sh

VERSION="$1"
if [ "$VERSION" = "" ]
then
	echo "$0 VERSION"
	exit 1
fi

if [ ! -f Makefile ]
then
	USEXYMONPING=y \
	ENABLESSL=y \
	ENABLELDAP=y \
	ENABLELDAPSSL=y \
	XYMONUSER=xymon \
	XYMONTOPDIR=/usr/lib/xymon \
	XYMONVAR=/var/lib/xymon \
	XYMONHOSTURL=/xymon \
	CGIDIR=/usr/lib/xymon/cgi-bin \
	XYMONCGIURL=/xymon-cgi \
	SECURECGIDIR=/usr/lib/xymon/cgi-secure \
	SECUREXYMONCGIURL=/xymon-seccgi \
	HTTPDGID=www-data \
	XYMONLOGDIR=/var/log/xymon \
	XYMONHOSTNAME=localhost \
	XYMONHOSTIP=127.0.0.1 \
	MANROOT=/usr/share/man \
	INSTALLBINDIR=/usr/lib/xymon/server/bin \
	INSTALLETCDIR=/etc/xymon \
	INSTALLWEBDIR=/etc/xymon/web \
	INSTALLEXTDIR=/usr/lib/xymon/server/ext \
	INSTALLTMPDIR=/var/lib/xymon/tmp \
	INSTALLWWWDIR=/var/lib/xymon/www \
	./configure
fi
if [ ! -x common/xymondigest ]
then
	make common-build
fi
./build/generate-md5.sh >build/md5.dat.new
mv build/md5.dat.new build/md5.dat

./build/updmanver $VERSION
./build/makehtml.sh $VERSION

exit 0

