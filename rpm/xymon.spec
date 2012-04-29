Name: xymon
Version: @VER@
Release: 1
Group: Networking/Daemons
URL: http://xymon.sourceforge.net/
License: GPL
Source: xymon-@VER@.tar.gz
Source1: xymon-init.d
Source2: xymon.logrotate
Source3: xymon-client.init
Source4: xymon-client.default
Summary: Xymon network monitor
BuildRoot: /tmp/xymon-root
#BuildRequires: openssl-devel
#BuildRequires: pcre-devel
#BuildRequires: rrdtool-devel
#BuildRequires: openldap-devel
Conflicts: xymon-client

%description
Xymon (previously known as Hobbit) is a system for monitoring 
your network servers and applications. This package contains 
the server side of the Xymon package.

%package client
Summary: Xymon client reporting data to the Xymon server
Group: Applications/System
Conflicts: xymon

%description client
This package contains a client for the Xymon (previously known
as Hobbit) monitor. Clients report data about the local system to 
the monitor, allowing it to check on the status of the system 
load, filesystem utilisation, processes that must be running etc.

%prep
rm -rf $RPM_BUILD_ROOT

%setup
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
        HTTPDGID=apache \
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

%build
	PKGBUILD=1 make

%install
        INSTALLROOT=$RPM_BUILD_ROOT PKGBUILD=1 make install
	mkdir -p $RPM_BUILD_ROOT/etc/init.d
	cp %{SOURCE1} $RPM_BUILD_ROOT/etc/init.d/xymon
	cp %{SOURCE3} $RPM_BUILD_ROOT/etc/init.d/xymon-client
	mkdir -p $RPM_BUILD_ROOT/etc/logrotate.d
	cp %{SOURCE2} $RPM_BUILD_ROOT/etc/logrotate.d/xymon
	mkdir -p $RPM_BUILD_ROOT/etc/default
	cp %{SOURCE4} $RPM_BUILD_ROOT/etc/default/xymon-client
	mkdir -p $RPM_BUILD_ROOT/usr/bin
	cd $RPM_BUILD_ROOT/usr/bin && ln -sf ../lib/xymon/server/bin/{xymon,xymoncmd} .
	mkdir -p $RPM_BUILD_ROOT/etc/httpd/conf.d
	mv $RPM_BUILD_ROOT/etc/xymon/xymon-apache.conf $RPM_BUILD_ROOT/etc/httpd/conf.d/
	rmdir $RPM_BUILD_ROOT/usr/lib/xymon/client/tmp
	cd $RPM_BUILD_ROOT/usr/lib/xymon/client && ln -sf /tmp tmp
	rmdir $RPM_BUILD_ROOT/usr/lib/xymon/client/logs
	cd $RPM_BUILD_ROOT/usr/lib/xymon/client && ln -sf ../../../../var/log/xymon logs
	mv $RPM_BUILD_ROOT/usr/lib/xymon/client/etc/xymonclient.cfg /tmp/xymonclient.cfg.$$
	cat /tmp/xymonclient.cfg.$$ | sed -e 's!^XYMSRV=.*!include /var/run/xymonclient-runtime.cfg!' | grep -v "^XYMSERVERS=" >$RPM_BUILD_ROOT/usr/lib/xymon/client/etc/xymonclient.cfg
	rm /tmp/xymonclient.cfg.$$

%clean
rm -rf $RPM_BUILD_ROOT


%pre
id xymon 1>/dev/null 2>&1
if [ $? -ne 0 ]
then
   groupadd xymon || true
   useradd -g xymon -c "Xymon user" -d /usr/lib/xymon xymon
fi
if [ -e /var/log/xymon/xymonlaunch.pid -a -x /etc/init.d/xymon ]
then
	/etc/init.d/xymon stop || true
fi

%pre client
id xymon 1>/dev/null 2>&1
if [ $? -ne 0 ]
then
   groupadd xymon || true
   useradd -g xymon -c "Xymon user" -d /usr/lib/xymon xymon
fi
if [ -e /var/log/xymon/clientlaunch.pid -a -x /etc/init.d/xymon-client ]
then
	/etc/init.d/xymon-client stop || true
fi


%post
chkconfig --add xymon

%post client
chkconfig --add xymon-client


%preun
if [ -e /var/log/xymon/xymonlaunch.pid -a -x /etc/init.d/xymon ]
then
	/etc/init.d/xymon stop || true
fi
chkconfig --del xymon

%preun client
if [ -e /var/log/xymon/clientlaunch.pid -a -x /etc/init.d/xymon-client ]
then
	/etc/init.d/xymon-client stop || true
fi
chkconfig --del xymon-client


%files
%attr(-, root, root) %doc README README.CLIENT Changes* COPYING CREDITS RELEASENOTES
%attr(644, root, root) %doc /usr/share/man/man*/*
%attr(644, root, root) %config /etc/xymon/*
%attr(644, root, root) %config /etc/httpd/conf.d/xymon-apache.conf
%attr(755, root, root) %dir /etc/xymon 
%attr(755, root, root) %dir /usr/lib/xymon/server/download
%attr(755, root, root) %dir /etc/xymon/web
%attr(755, xymon, xymon) %dir /var/log/xymon
%attr(755, root, root) /etc/init.d/xymon
%attr(644, root, root) /etc/logrotate.d/xymon
%attr(-, root, root) /usr/lib/xymon
%attr(-, root, root) /usr/bin/*
%attr(-, xymon, xymon) /var/lib/xymon
%attr(775, xymon, apache) %dir /var/lib/xymon/www/rep
%attr(775, xymon, apache) %dir /var/lib/xymon/www/snap
%attr(644, root, root) %config /var/lib/xymon/www/menu/xymonmenu.css
%attr(755, xymon, xymon) %dir /usr/lib/xymon/client/ext
%attr(664, xymon, apache) %config /etc/xymon/critical.cfg
%attr(664, xymon, apache) %config /etc/xymon/critical.cfg.bak
%attr(4750, root, xymon) /usr/lib/xymon/server/bin/fping4
%attr(4750, root, xymon) /usr/lib/xymon/server/bin/fping6
%attr(750, root, xymon) /usr/lib/xymon/client/bin/logfetch
%attr(750, root, xymon) /usr/lib/xymon/client/bin/clientupdate

%files client
%attr(-, root, root) %doc README README.CLIENT Changes* COPYING CREDITS RELEASENOTES
%attr(-, root, root) /usr/lib/xymon/client
%attr(755, root, root) /etc/init.d/xymon-client
%attr(644, root, root) %config /etc/default/xymon-client
%attr(755, xymon, xymon) %dir /var/log/xymon
%attr(755, xymon, xymon) %dir /usr/lib/xymon/client/ext
%attr(750, root, xymon) /usr/lib/xymon/client/bin/logfetch
%attr(750, root, xymon) /usr/lib/xymon/client/bin/clientupdate

