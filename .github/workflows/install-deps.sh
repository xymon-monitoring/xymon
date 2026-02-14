#!/bin/sh

uname -a | grep -qi freebsd
if [ $? -eq 0 ];then
	sudo pw user add -n xymon -c 'xymon' -d /home/xymon -G wheel -m -s /usr/local/bin/bash || exit $?
fi

if [ -x /usr/sbin/useradd ];then
	echo "DEBUG: create xymon with useradd"
	sudo /usr/sbin/useradd --help
	sudo /usr/sbin/useradd xymon
fi
if [ -x /usr/sbin/adduser ];then
	echo "DEBUG: create xymon with adduser"
	grep -q xymon /etc/passwd
	if [ $? -eq 0 ];then
		echo "DEBUG: xymon already exists"
	else
		sudo /usr/sbin/adduser --help
		sudo /usr/sbin/adduser -w no -s /bin/sh -q xymon
	fi
fi

if [ -x  /usr/sbin/pkg ];then
	echo "DEBUG: using pkg"
	sudo -E ASSUME_ALWAYS_YES=YES pkg install gmake c-ares pcre fping autotools
	exit $?
fi
GCC=gcc
if [ -x /usr/sbin/pkg_info ];then
	uname -a | grep -qi openbsd
	if [ $? -eq 0 ];then
		echo "DEBUG: search with pkg_info -Q"
		/usr/sbin/pkg_info -Q c-ares
		/usr/sbin/pkg_info -Q cares
		/usr/sbin/pkg_info -Q gmake
		/usr/sbin/pkg_info -Q pcre
		/usr/sbin/pkg_info -Q gcc
		/usr/sbin/pkg_info -Q fping
		GCC="$(/usr/sbin/pkg_info -Q gcc | grep '^gcc-[0-9]' | sort --version-sort | tail -n1)"
		P_AUTOMAKE="$(/usr/sbin/pkg_info -Q automake | sort --version-sort | tail -n1)"
		P_AUTOCONF="$(/usr/sbin/pkg_info -Q autoconf | grep 'autoconf-[0-9]' | sort --version-sort | tail -n1)"
	else
		echo "DEBUG: search with pkg_info"
		/usr/sbin/pkg_info c-ares
		/usr/sbin/pkg_info gmake
		/usr/sbin/pkg_info pcre
		/usr/sbin/pkg_info fping
	fi
fi

which pkgin
if [ -x /usr/pkg/bin/pkgin ];then
	echo "DEBUG: using pkgin"

	sudo -E /usr/pkg/bin/pkgin -y install gmake libcares pcre fping autoconf automake || exit $?

	exit 0
fi

if [ -x /usr/sbin/pkg_add ];then
	echo "DEBUG: using pkg_add to install $GCC"
	sudo -E /usr/sbin/pkg_add gmake cares pcre $GCC fping $P_AUTOMAKE $P_AUTOCONF || exit $?

	exit 0
fi

exit 0
