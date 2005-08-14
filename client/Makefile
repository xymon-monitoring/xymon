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

COMMONTOOLS=bb bbcmd bbhostgrep bbhostshow

all: hobbitlaunch $(COMMONTOOLS) hobbitclient.cfg $(EXTRATOOLS)

hobbitclient.cfg: hobbitclient.cfg.DIST
	cat hobbitclient.cfg.DIST | sed -e's!@BBHOSTIP@!$(BBHOSTIP)!g' >hobbitclient.cfg
	../build/bb-commands.sh >>hobbitclient.cfg

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
	cp -fp runclient.sh $(INSTALLROOT)$(BBHOME)
	cp -fp hobbitlaunch hobbitclient*.sh $(COMMONTOOLS) $(EXTRATOOLS) $(INSTALLROOT)$(BBHOME)/bin/
	../build/merge-sects clientlaunch.cfg $(INSTALLROOT)$(BBHOME)/etc/clientlaunch.cfg
	../build/merge-lines hobbitclient.cfg $(INSTALLROOT)$(BBHOME)/etc/hobbitclient.cfg
ifndef PKGBUILD
	chown -R $(BBUSER) $(INSTALLROOT)$(BBHOME)
endif

clean:
	rm -f hobbitlaunch $(COMMONTOOLS) hobbitclient.cfg $(EXTRATOOLS) *~

