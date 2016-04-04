OSTYPE := $(shell uname -s | tr '[A-Z]' '[a-z]')

#ifeq ($(OSTYPE),hpux)
#	EXTRATOOLS=hpux-meminfo
#endif
#ifeq ($(OSTYPE),hp-ux)
#	EXTRATOOLS=hpux-meminfo
#endif
ifeq ($(OSTYPE),freebsd)
	EXTRATOOLS=freebsd-meminfo
endif
ifeq ($(OSTYPE),netbsd)
	EXTRATOOLS=netbsd-meminfo
endif
ifeq ($(OSTYPE),openbsd)
	EXTRATOOLS=openbsd-meminfo
endif

XYMONCLIENTLIB = ../lib/libxymonclient.a
XYMONCLIENTLIBS = -lxymonclient
XYMONCLIENTCOMMLIB = ../lib/libxymonclientcomm.a
XYMONCLIENTCOMMLIBS = -lxymonclientcomm $(COMPLIBS) $(SSLLIBS) $(NETLIBS) $(LIBRTDEF)
XYMONTIMELIB = ../lib/libxymontime.a
XYMONTIMELIBS = $(XYMONTIMELIB) $(LIBRTDEF)

PROGRAMS=xymonlaunch logfetch clientupdate orcaxymon msgcache
COMMONTOOLS=xymon xymoncmd xymongrep xymoncfg xymondigest

all: $(PROGRAMS) $(COMMONTOOLS) xymonclient.cfg clientlaunch.cfg $(EXTRATOOLS)

xymonclient.cfg: xymonclient.cfg.DIST
	cat xymonclient.cfg.DIST | sed -e 's!@XYMONTOPDIR@!$(XYMONTOPDIR)!g' | sed -e 's!@XYMONHOSTIP@!$(XYMONHOSTIP)!g' >xymonclient.cfg
	../build/bb-commands.sh >>xymonclient.cfg

clientlaunch.cfg: clientlaunch.cfg.DIST
ifeq ($(LOCALCLIENT),yes)
	cat clientlaunch.cfg.DIST | sed -e 's!@CLIENTFLAGS@!--local!g' >clientlaunch.cfg
else
	cat clientlaunch.cfg.DIST | sed -e 's!@CLIENTFLAGS@!!g' >clientlaunch.cfg
endif

logfetch: logfetch.c $(XYMONCLIENTLIB)
	$(CC) $(CFLAGS) -o $@ logfetch.c $(XYMONCLIENTLIBS)

clientupdate: clientupdate.c $(XYMONCLIENTCOMMLIB) $(XYMONCLIENTLIB)
	$(CC) $(CFLAGS) -o $@ clientupdate.c $(XYMONCLIENTCOMMLIBS) $(XYMONCLIENTLIBS)

orcaxymon: orcaxymon.c $(XYMONCLIENTCOMMLIB) $(XYMONCLIENTLIB)
	$(CC) $(CFLAGS) -o $@ orcaxymon.c $(XYMONCLIENTCOMMLIBS) $(XYMONCLIENTLIBS)

msgcache: msgcache.c $(XYMONCLIENTLIB)
	$(CC) $(CFLAGS) -o $@ msgcache.c $(XYMONCLIENTCOMMLIBS) $(XYMONCLIENTLIBS)

hpux-meminfo: hpux-meminfo.c
	$(CC) -o $@ hpux-meminfo.c

freebsd-meminfo: freebsd-meminfo.c
	$(CC) -o $@ freebsd-meminfo.c

openbsd-meminfo: openbsd-meminfo.c
	$(CC) -o $@ openbsd-meminfo.c

netbsd-meminfo: netbsd-meminfo.c
	$(CC) -o $@ netbsd-meminfo.c

install:
	if test ! -d $(INSTALLROOT)$(XYMONHOME)       ; then mkdir -p $(INSTALLROOT)$(XYMONHOME)       ; chmod 755 $(INSTALLROOT)$(XYMONHOME)      ; fi
	if test ! -d $(INSTALLROOT)$(XYMONHOME)/bin   ; then mkdir -p $(INSTALLROOT)$(XYMONHOME)/bin   ; chmod 755 $(INSTALLROOT)$(XYMONHOME)/bin  ; fi
	if test ! -d $(INSTALLROOT)$(XYMONHOME)/etc   ; then mkdir -p $(INSTALLROOT)$(XYMONHOME)/etc   ; chmod 755 $(INSTALLROOT)$(XYMONHOME)/etc  ; fi
	if test ! -d $(INSTALLROOT)$(XYMONHOME)/tmp   ; then mkdir -p $(INSTALLROOT)$(XYMONHOME)/tmp   ; chmod 755 $(INSTALLROOT)$(XYMONHOME)/tmp  ; fi
	if test ! -d $(INSTALLROOT)$(XYMONHOME)/logs  ; then mkdir -p $(INSTALLROOT)$(XYMONHOME)/logs  ; chmod 755 $(INSTALLROOT)$(XYMONHOME)/logs ; fi
	if test ! -d $(INSTALLROOT)$(XYMONHOME)/ext   ; then mkdir -p $(INSTALLROOT)$(XYMONHOME)/ext   ; chmod 755 $(INSTALLROOT)$(XYMONHOME)/ext  ; fi
	if test ! -d $(INSTALLROOT)$(XYMONHOME)/local ; then mkdir -p $(INSTALLROOT)$(XYMONHOME)/local ; chmod 755 $(INSTALLROOT)$(XYMONHOME)/local; fi
	if test ! -d $(INSTALLROOT)$(XYMONHOME)/sections ; then mkdir -p $(INSTALLROOT)$(XYMONHOME)/sections ; chmod 755 $(INSTALLROOT)$(XYMONHOME)/sections; fi
	if test ! -f $(INSTALLROOT)$(XYMONHOME)/etc/localclient.cfg ; then cp localclient.cfg $(INSTALLROOT)$(XYMONHOME)/etc/ ; chmod 644 $(INSTALLROOT)$(XYMONHOME)/etc/localclient.cfg; fi
	if test ! -f $(INSTALLROOT)$(XYMONHOME)/local/README; then cp README-local $(INSTALLROOT)$(XYMONHOME)/local/README ; chmod 644 $(INSTALLROOT)$(XYMONHOME)/local/README; fi
	if test ! -f $(INSTALLROOT)$(XYMONHOME)/sections/README; then cp README-sections $(INSTALLROOT)$(XYMONHOME)/sections/README ; chmod 644 $(INSTALLROOT)$(XYMONHOME)/sections/README; fi
	chmod 755 runclient.sh $(PROGRAMS) xymonclient*.sh $(COMMONTOOLS) $(EXTRATOOLS)
	cp -fp runclient.sh $(INSTALLROOT)$(XYMONHOME)
	cp -fp $(PROGRAMS) xymonclient*.sh $(COMMONTOOLS) $(EXTRATOOLS) $(INSTALLROOT)$(XYMONHOME)/bin/

	../build/merge-sects clientlaunch.cfg $(INSTALLROOT)$(XYMONHOME)/etc/clientlaunch.cfg
	../build/merge-lines xymonclient.cfg $(INSTALLROOT)$(XYMONHOME)/etc/xymonclient.cfg
	chmod 644 $(INSTALLROOT)$(XYMONHOME)/etc/clientlaunch.cfg $(INSTALLROOT)$(XYMONHOME)/etc/xymonclient.cfg

ifndef PKGBUILD
	chown -R $(XYMONUSER) $(INSTALLROOT)$(XYMONHOME)
	chgrp -R `$(IDTOOL) -g $(XYMONUSER)` $(INSTALLROOT)$(XYMONHOME)
	chmod 755 $(INSTALLROOT)$(XYMONHOME)
endif

install-localclient:
	chmod 755 xymond_client
	cp -fp xymond_client $(INSTALLROOT)$(XYMONHOME)/bin/
ifndef PKGBUILD
	chown $(XYMONUSER) $(INSTALLROOT)$(XYMONHOME)/bin/xymond_client
	chgrp `$(IDTOOL) -g $(XYMONUSER)` $(INSTALLROOT)$(XYMONHOME)/bin/xymond_client
endif

clean:
	rm -f $(PROGRAMS) $(COMMONTOOLS) xymond_client xymonclient.cfg clientlaunch.cfg $(EXTRATOOLS) *~

