Name: hobbit
Version: @VER@
Release: 1
Group: Networking/Daemons
URL: http://hobbitmon.sourceforge.net/
License: GPL
Source: hobbit-@VER@.tar.gz
Source1: hobbit-init.d
Source2: hobbit.logrotate
Summary: Hobbit network monitor
BuildRoot: /tmp/hobbit-root
Requires: fping
# BuildRequires: openssl-devel, pcre-devel, rrdtool-devel, openldap-devel

%description
Hobbit is a system for monitoring your network servers and
applications. It is heavily inspired by the Big Brother
tool, but is a complete re-implementation with a lot of added
functionality and performance improvements.

%prep
rm -rf $RPM_BUILD_ROOT

%setup
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
        BARS=all \
        USENEWHIST=y \
        PIXELCOUNT=960 \
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
	mkdir -p $RPM_BUILD_ROOT/etc/logrotate.d
	cp %{SOURCE2} $RPM_BUILD_ROOT/etc/logrotate.d/hobbit
	mkdir -p $RPM_BUILD_ROOT/usr/bin
	cd $RPM_BUILD_ROOT/usr/bin && ln -s ../lib/hobbit/server/bin/{bb,bbcmd} .

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



%post
chkconfig --add hobbit


%preun
if [ -e /var/log/hobbit/hobbitlaunch.pid -a -x /etc/init.d/hobbit ]
then
	/etc/init.d/hobbit stop || true
fi
chkconfig --del hobbit


%files
%attr(-, root, root) %doc README Changes* COPYING CREDITS
%attr(644, root, root) %doc /usr/share/man/man*/*
%attr(644, root, root) %config /etc/hobbit/*
%attr(755, root, root) %dir /etc/hobbit 
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

