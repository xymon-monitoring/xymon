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
XYMONCLIENTLIBS = $(XYMONCLIENTLIB)
XYMONCLIENTCOMMLIB = ../lib/libxymonclientcomm.a
XYMONCLIENTCOMMLIBS = $(XYMONCLIENTCOMMLIB) $(SSLLIBS) $(ZLIBLIBS) $(NETLIBS) $(LIBRTDEF)

PROGRAMS=xymonlaunch logfetch clientupdate orcaxymon msgcache
COMMONTOOLS=xymon xymoncmd xymongrep xymoncfg xymondigest

all: $(PROGRAMS) $(COMMONTOOLS) xymonclient.cfg clientlaunch.cfg $(EXTRATOOLS)

xymonclient.cfg: xymonclient.cfg.DIST
	cat xymonclient.cfg.DIST | sed -e 's!@XYMONHOSTIP@!$(XYMONHOSTIP)!g' | sed -e 's!@XYMONHOME@!$(XYMONHOME)!g' >xymonclient.cfg
	../build/bb-commands.sh >>xymonclient.cfg

clientlaunch.cfg: clientlaunch.cfg.DIST
ifeq ($(LOCALCLIENT),yes)
	cat clientlaunch.cfg.DIST | sed -e 's!@CLIENTFLAGS@!--local!g' | sed -e 's!@XYMONHOME@!$(XYMONHOME)!g' >clientlaunch.cfg
else
	cat clientlaunch.cfg.DIST | sed -e 's!@CLIENTFLAGS@!!g' | sed -e 's!@XYMONHOME@!$(XYMONHOME)!g' >clientlaunch.cfg
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

# Set by build/Makefile.rules; defaulted here so that running this
# makefile directly behaves as it always did.
ifndef INSTALLCLIENTETCDIR
INSTALLCLIENTETCDIR = $(XYMONHOME)/etc
endif
ifndef INSTALLCLIENTBINDIR
INSTALLCLIENTBINDIR = $(XYMONHOME)/bin
endif
ifndef INSTALLCLIENTTMPDIR
INSTALLCLIENTTMPDIR = $(XYMONHOME)/tmp
endif
ifndef INSTALLCLIENTLOGDIR
INSTALLCLIENTLOGDIR = $(XYMONHOME)/logs
endif

install:
	if test ! -d $(INSTALLROOT)$(XYMONHOME)       ; then mkdir -p $(INSTALLROOT)$(XYMONHOME)       ; chmod 755 $(INSTALLROOT)$(XYMONHOME)      ; fi
	if test ! -d $(INSTALLROOT)$(INSTALLCLIENTBINDIR)   ; then mkdir -p $(INSTALLROOT)$(INSTALLCLIENTBINDIR)   ; chmod 755 $(INSTALLROOT)$(INSTALLCLIENTBINDIR)  ; fi
	if test ! -d $(INSTALLROOT)$(INSTALLCLIENTETCDIR)   ; then mkdir -p $(INSTALLROOT)$(INSTALLCLIENTETCDIR)   ; chmod 755 $(INSTALLROOT)$(INSTALLCLIENTETCDIR)  ; fi
	if test ! -d $(INSTALLROOT)$(INSTALLCLIENTTMPDIR)   ; then mkdir -p $(INSTALLROOT)$(INSTALLCLIENTTMPDIR)   ; chmod 755 $(INSTALLROOT)$(INSTALLCLIENTTMPDIR)  ; fi
	if test ! -d $(INSTALLROOT)$(INSTALLCLIENTLOGDIR)  ; then mkdir -p $(INSTALLROOT)$(INSTALLCLIENTLOGDIR)  ; chmod 755 $(INSTALLROOT)$(INSTALLCLIENTLOGDIR) ; fi
	if test ! -d $(INSTALLROOT)$(XYMONHOME)/ext   ; then mkdir -p $(INSTALLROOT)$(XYMONHOME)/ext   ; chmod 755 $(INSTALLROOT)$(XYMONHOME)/ext  ; fi
	if test ! -d $(INSTALLROOT)$(XYMONHOME)/local ; then mkdir -p $(INSTALLROOT)$(XYMONHOME)/local ; chmod 755 $(INSTALLROOT)$(XYMONHOME)/local; fi
	if test ! -f $(INSTALLROOT)$(INSTALLCLIENTETCDIR)/localclient.cfg ; then cp localclient.cfg $(INSTALLROOT)$(INSTALLCLIENTETCDIR)/ ; chmod 644 $(INSTALLROOT)$(INSTALLCLIENTETCDIR)/localclient.cfg; fi
	if test ! -f $(INSTALLROOT)$(XYMONHOME)/local/README; then cp README-local $(INSTALLROOT)$(XYMONHOME)/local/README ; chmod 644 $(INSTALLROOT)$(XYMONHOME)/local/README; fi
	chmod 755 runclient.sh $(PROGRAMS) xymonclient*.sh $(COMMONTOOLS) $(EXTRATOOLS)
	cp -fp runclient.sh $(INSTALLROOT)$(XYMONHOME)
	cp -fp $(PROGRAMS) xymonclient*.sh $(COMMONTOOLS) $(EXTRATOOLS) $(INSTALLROOT)$(INSTALLCLIENTBINDIR)/

	../build/merge-sects clientlaunch.cfg $(INSTALLROOT)$(INSTALLCLIENTETCDIR)/clientlaunch.cfg
	../build/merge-lines xymonclient.cfg $(INSTALLROOT)$(INSTALLCLIENTETCDIR)/xymonclient.cfg
	chmod 644 $(INSTALLROOT)$(INSTALLCLIENTETCDIR)/clientlaunch.cfg $(INSTALLROOT)$(INSTALLCLIENTETCDIR)/xymonclient.cfg

ifndef PKGBUILD
	chown -R $(XYMONUSER) $(INSTALLROOT)$(XYMONHOME)
	chgrp -R `$(IDTOOL) -g $(XYMONUSER)` $(INSTALLROOT)$(XYMONHOME)
	chmod 755 $(INSTALLROOT)$(XYMONHOME)
endif

install-localclient:
	chmod 755 xymond_client
	cp -fp xymond_client $(INSTALLROOT)$(INSTALLCLIENTBINDIR)/
ifndef PKGBUILD
	chown $(XYMONUSER) $(INSTALLROOT)$(INSTALLCLIENTBINDIR)/xymond_client
	chgrp `$(IDTOOL) -g $(XYMONUSER)` $(INSTALLROOT)$(INSTALLCLIENTBINDIR)/xymond_client
endif

clean:
	rm -f $(PROGRAMS) $(COMMONTOOLS) xymond_client xymonclient.cfg clientlaunch.cfg $(EXTRATOOLS) *~

