Name: hobbit
Version: @VER@
Release: 1
Group: Networking/Daemons
URL: http://xymon.sourceforge.net/
License: GPL
Source: hobbit-@VER@.tar.gz
Source1: hobbit-init.d
Source2: hobbit.logrotate
Source3: hobbit-client.init
Source4: hobbit-client.default
Summary: Hobbit network monitor
BuildRoot: /tmp/hobbit-root
#BuildRequires: openssl-devel
#BuildRequires: pcre-devel
#BuildRequires: rrdtool-devel
#BuildRequires: openldap-devel
Conflicts: hobbit-client

%description
Hobbit is a system for monitoring your network servers and
applications. This package contains the server side of the
Hobbit package.

%package client
Summary: Hobbit client reporting data to the Hobbit server
Group: Applications/System
Conflicts: hobbit

%description client
This package contains a client for the Hobbit monitor. Clients
report data about the local system to the monitor, allowing it
to check on the status of the system load, filesystem utilisation,
processes that must be running etc.

%prep
rm -rf $RPM_BUILD_ROOT

%setup
	USEHOBBITPING=y \
        ENABLESSL=y \
        ENABLELDAP=y \
        ENABLELDAPSSL=y \
        BBUSER=hobbit \
        BBTOPDIR=/usr/lib/hobbit \
        BBVAR=/var/lib/hobbit \
        BBHOSTURL=/hobbit \
        CGIDIR=/usr/lib/hobbit/cgi-bin \
        BBCGIURL=/hobbit-cgi \
        SECURECGIDIR=/usr/lib/hobbit/cgi-secure \
        SECUREBBCGIURL=/hobbit-seccgi \
        HTTPDGID=apache \
        BBLOGDIR=/var/log/hobbit \
        BBHOSTNAME=localhost \
        BBHOSTIP=127.0.0.1 \
        MANROOT=/usr/share/man \
        INSTALLBINDIR=/usr/lib/hobbit/server/bin \
        INSTALLETCDIR=/etc/hobbit \
        INSTALLWEBDIR=/etc/hobbit/web \
        INSTALLEXTDIR=/usr/lib/hobbit/server/ext \
        INSTALLTMPDIR=/var/lib/hobbit/tmp \
        INSTALLWWWDIR=/var/lib/hobbit/www \
        ./configure

%build
	PKGBUILD=1 make

%install
        INSTALLROOT=$RPM_BUILD_ROOT PKGBUILD=1 make install
	mkdir -p $RPM_BUILD_ROOT/etc/init.d
	cp %{SOURCE1} $RPM_BUILD_ROOT/etc/init.d/hobbit
	cp %{SOURCE3} $RPM_BUILD_ROOT/etc/init.d/hobbit-client
	mkdir -p $RPM_BUILD_ROOT/etc/logrotate.d
	cp %{SOURCE2} $RPM_BUILD_ROOT/etc/logrotate.d/hobbit
	mkdir -p $RPM_BUILD_ROOT/etc/default
	cp %{SOURCE4} $RPM_BUILD_ROOT/etc/default/hobbit-client
	mkdir -p $RPM_BUILD_ROOT/usr/bin
	cd $RPM_BUILD_ROOT/usr/bin && ln -sf ../lib/hobbit/server/bin/{bb,bbcmd} .
	mkdir -p $RPM_BUILD_ROOT/etc/httpd/conf.d
	mv $RPM_BUILD_ROOT/etc/hobbit/hobbit-apache.conf $RPM_BUILD_ROOT/etc/httpd/conf.d/
	rmdir $RPM_BUILD_ROOT/usr/lib/hobbit/client/tmp
	cd $RPM_BUILD_ROOT/usr/lib/hobbit/client && ln -sf /tmp tmp
	rmdir $RPM_BUILD_ROOT/usr/lib/hobbit/client/logs
	cd $RPM_BUILD_ROOT/usr/lib/hobbit/client && ln -sf ../../../../var/log/hobbit logs
	mv $RPM_BUILD_ROOT/usr/lib/hobbit/client/etc/hobbitclient.cfg /tmp/hobbitclient.cfg.$$
	cat /tmp/hobbitclient.cfg.$$ | sed -e 's!^BBDISP=.*!include /var/run/hobbitclient-runtime.cfg!' | grep -v "^BBDISPLAYS=" >$RPM_BUILD_ROOT/usr/lib/hobbit/client/etc/hobbitclient.cfg
	rm /tmp/hobbitclient.cfg.$$

%clean
rm -rf $RPM_BUILD_ROOT


%pre
id hobbit 1>/dev/null 2>&1
if [ $? -ne 0 ]
then
   groupadd hobbit || true
   useradd -g hobbit -c "Hobbit user" -d /usr/lib/hobbit hobbit
fi
if [ -e /var/log/hobbit/hobbitlaunch.pid -a -x /etc/init.d/hobbit ]
then
	/etc/init.d/hobbit stop || true
fi

%pre client
id hobbit 1>/dev/null 2>&1
if [ $? -ne 0 ]
then
   groupadd hobbit || true
   useradd -g hobbit -c "Hobbit user" -d /usr/lib/hobbit hobbit
fi
if [ -e /var/log/hobbit/clientlaunch.pid -a -x /etc/init.d/hobbit-client ]
then
	/etc/init.d/hobbit-client stop || true
fi


%post
chkconfig --add hobbit

%post client
chkconfig --add hobbit-client


%preun
if [ -e /var/log/hobbit/hobbitlaunch.pid -a -x /etc/init.d/hobbit ]
then
	/etc/init.d/hobbit stop || true
fi
chkconfig --del hobbit

%preun client
if [ -e /var/log/hobbit/clientlaunch.pid -a -x /etc/init.d/hobbit-client ]
then
	/etc/init.d/hobbit-client stop || true
fi
chkconfig --del hobbit-client


%files
%attr(-, root, root) %doc README README.CLIENT Changes* COPYING CREDITS RELEASENOTES
%attr(644, root, root) %doc /usr/share/man/man*/*
%attr(644, root, root) %config /etc/hobbit/*
%attr(644, root, root) %config /etc/httpd/conf.d/hobbit-apache.conf
%attr(755, root, root) %dir /etc/hobbit 
%attr(755, root, root) %dir /usr/lib/hobbit/server/download
%attr(755, root, root) %dir /etc/hobbit/web
%attr(755, hobbit, hobbit) %dir /var/log/hobbit
%attr(755, root, root) /etc/init.d/hobbit
%attr(644, root, root) /etc/logrotate.d/hobbit
%attr(-, root, root) /usr/lib/hobbit
%attr(-, root, root) /usr/bin/*
%attr(-, hobbit, hobbit) /var/lib/hobbit
%attr(775, hobbit, apache) %dir /var/lib/hobbit/www/rep
%attr(775, hobbit, apache) %dir /var/lib/hobbit/www/snap
%attr(644, root, root) %config /var/lib/hobbit/www/menu/menu_items.js
%attr(644, root, root) %config /var/lib/hobbit/www/menu/menu_tpl.js
%attr(644, root, root) %config /var/lib/hobbit/www/menu/menu.css
%attr(755, hobbit, hobbit) %dir /usr/lib/hobbit/client/ext
%attr(664, hobbit, apache) %config /etc/hobbit/hobbit-nkview.cfg
%attr(664, hobbit, apache) %config /etc/hobbit/hobbit-nkview.cfg.bak
%attr(4750, root, hobbit) /usr/lib/hobbit/server/bin/hobbitping
%attr(750, root, hobbit) /usr/lib/hobbit/client/bin/logfetch
%attr(750, root, hobbit) /usr/lib/hobbit/client/bin/clientupdate

%files client
%attr(-, root, root) %doc README README.CLIENT Changes* COPYING CREDITS RELEASENOTES
%attr(-, root, root) /usr/lib/hobbit/client
%attr(755, root, root) /etc/init.d/hobbit-client
%attr(644, root, root) %config /etc/default/hobbit-client
%attr(755, hobbit, hobbit) %dir /var/log/hobbit
%attr(755, hobbit, hobbit) %dir /usr/lib/hobbit/client/ext
%attr(750, root, hobbit) /usr/lib/hobbit/client/bin/logfetch
%attr(750, root, hobbit) /usr/lib/hobbit/client/bin/clientupdate

