OSTYPE := $(shell uname -s | tr '[A-Z]' '[a-z]')

ifeq ($(OSTYPE),hpux)
	EXTRATOOLS=hpux-meminfo
endif
ifeq ($(OSTYPE),hp-ux)
	EXTRATOOLS=hpux-meminfo
endif
ifeq ($(OSTYPE),freebsd)
	EXTRATOOLS=freebsd-meminfo
endif
ifeq ($(OSTYPE),netbsd)
	EXTRATOOLS=netbsd-meminfo
endif
ifeq ($(OSTYPE),openbsd)
	EXTRATOOLS=openbsd-meminfo
endif

PROGRAMS=xymonlaunch logfetch clientupdate orcaxymon msgcache
COMMONTOOLS=xymon xymoncmd xymongrep xymoncfg xymondigest

all: $(PROGRAMS) $(COMMONTOOLS) xymonclient.cfg clientlaunch.cfg $(EXTRATOOLS)

xymonclient.cfg: xymonclient.cfg.DIST
	cat xymonclient.cfg.DIST | sed -e 's!@BBHOSTIP@!$(BBHOSTIP)!g' >xymonclient.cfg
	../build/bb-commands.sh >>xymonclient.cfg

clientlaunch.cfg: clientlaunch.cfg.DIST
ifeq ($(LOCALCLIENT),yes)
	cat clientlaunch.cfg.DIST | sed -e 's!@CLIENTFLAGS@!--local!g' >clientlaunch.cfg
else
	cat clientlaunch.cfg.DIST | sed -e 's!@CLIENTFLAGS@!!g' >clientlaunch.cfg
endif

logfetch: logfetch.c
	$(CC) $(CFLAGS) -o $@ logfetch.c ../lib/xymonclient.a $(LIBRTDEF)

clientupdate: clientupdate.c
	$(CC) $(CFLAGS) -o $@ clientupdate.c ../lib/xymonclient.a $(NETLIBS) $(LIBRTDEF)

orcaxymon: orcaxymon.c
	$(CC) $(CFLAGS) -o $@ orcaxymon.c ../lib/xymonclient.a $(NETLIBS) $(LIBRTDEF)

msgcache: msgcache.c
	$(CC) $(CFLAGS) -o $@ msgcache.c ../lib/xymonclient.a $(NETLIBS) $(LIBRTDEF)

hpux-meminfo: hpux-meminfo.c
	$(CC) -o $@ hpux-meminfo.c

freebsd-meminfo: freebsd-meminfo.c
	$(CC) -o $@ freebsd-meminfo.c

openbsd-meminfo: openbsd-meminfo.c
	$(CC) -o $@ openbsd-meminfo.c

netbsd-meminfo: netbsd-meminfo.c
	$(CC) -o $@ netbsd-meminfo.c

install:
	if test ! -d $(INSTALLROOT)$(BBHOME)     ; then mkdir -p $(INSTALLROOT)$(BBHOME)     ; fi
	if test ! -d $(INSTALLROOT)$(BBHOME)/bin ; then mkdir -p $(INSTALLROOT)$(BBHOME)/bin ; fi
	if test ! -d $(INSTALLROOT)$(BBHOME)/etc ; then mkdir -p $(INSTALLROOT)$(BBHOME)/etc ; fi
	if test ! -d $(INSTALLROOT)$(BBHOME)/tmp ; then mkdir -p $(INSTALLROOT)$(BBHOME)/tmp ; fi
	if test ! -d $(INSTALLROOT)$(BBHOME)/logs; then mkdir -p $(INSTALLROOT)$(BBHOME)/logs; fi
	if test ! -d $(INSTALLROOT)$(BBHOME)/ext ; then mkdir -p $(INSTALLROOT)$(BBHOME)/ext ; fi
	if test ! -f $(INSTALLROOT)$(BBHOME)/etc/localclient.cfg ; then cp localclient.cfg $(INSTALLROOT)$(BBHOME)/etc/ ; fi
	cp -fp runclient.sh $(INSTALLROOT)$(BBHOME)
	cp -fp $(PROGRAMS) xymonclient*.sh $(COMMONTOOLS) $(EXTRATOOLS) $(INSTALLROOT)$(BBHOME)/bin/
	chmod ugo+x $(INSTALLROOT)$(BBHOME)/bin/*

	../build/merge-sects clientlaunch.cfg $(INSTALLROOT)$(BBHOME)/etc/clientlaunch.cfg
	../build/merge-lines xymonclient.cfg $(INSTALLROOT)$(BBHOME)/etc/xymonclient.cfg
ifndef PKGBUILD
	chown -R $(BBUSER) $(INSTALLROOT)$(BBHOME)
endif

install-localclient:
	cp -fp xymond_client $(INSTALLROOT)$(BBHOME)/bin/
ifndef PKGBUILD
	chown $(BBUSER) $(INSTALLROOT)$(BBHOME)/bin/xymond_client
endif

clean:
	rm -f $(PROGRAMS) $(COMMONTOOLS) xymond_client xymonclient.cfg clientlaunch.cfg $(EXTRATOOLS) *~

