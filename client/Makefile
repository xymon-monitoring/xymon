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

PROGRAMS=hobbitlaunch logfetch clientupdate orcahobbit msgcache
COMMONTOOLS=bb bbcmd bbhostgrep bbhostshow bbdigest

all: $(PROGRAMS) $(COMMONTOOLS) hobbitclient.cfg clientlaunch.cfg $(EXTRATOOLS)

hobbitclient.cfg: hobbitclient.cfg.DIST
	cat hobbitclient.cfg.DIST | sed -e 's!@BBHOSTIP@!$(BBHOSTIP)!g' >hobbitclient.cfg
	../build/bb-commands.sh >>hobbitclient.cfg

clientlaunch.cfg: clientlaunch.cfg.DIST
ifeq ($(LOCALCLIENT),yes)
	cat clientlaunch.cfg.DIST | sed -e 's!@CLIENTFLAGS@!--local!g' >clientlaunch.cfg
else
	cat clientlaunch.cfg.DIST | sed -e 's!@CLIENTFLAGS@!!g' >clientlaunch.cfg
endif

logfetch: logfetch.c
	$(CC) $(CFLAGS) -o $@ logfetch.c ../lib/hobbitclient.a

clientupdate: clientupdate.c
	$(CC) $(CFLAGS) -o $@ clientupdate.c ../lib/hobbitclient.a $(HOBBITZLIB) $(SSLLIBS) $(NETLIBS)

orcahobbit: orcahobbit.c
	$(CC) $(CFLAGS) -o $@ orcahobbit.c ../lib/hobbitclient.a $(HOBBITZLIB) $(SSLLIBS) $(NETLIBS)

msgcache: msgcache.c
	$(CC) $(CFLAGS) -o $@ msgcache.c ../lib/hobbitclient.a $(HOBBITZLIB) $(NETLIBS)

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
	cp -fp $(PROGRAMS) hobbitclient*.sh $(COMMONTOOLS) $(EXTRATOOLS) $(INSTALLROOT)$(BBHOME)/bin/
	chmod ugo+x $(INSTALLROOT)$(BBHOME)/bin/*

	../build/merge-sects clientlaunch.cfg $(INSTALLROOT)$(BBHOME)/etc/clientlaunch.cfg
	../build/merge-lines hobbitclient.cfg $(INSTALLROOT)$(BBHOME)/etc/hobbitclient.cfg
ifndef PKGBUILD
	chown -R $(BBUSER) $(INSTALLROOT)$(BBHOME)
endif

install-localclient:
	cp -fp hobbitd_client $(INSTALLROOT)$(BBHOME)/bin/
ifndef PKGBUILD
	chown $(BBUSER) $(INSTALLROOT)$(BBHOME)/bin/hobbitd_client
endif

clean:
	rm -f $(PROGRAMS) $(COMMONTOOLS) hobbitd_client hobbitclient.cfg clientlaunch.cfg $(EXTRATOOLS) *~

