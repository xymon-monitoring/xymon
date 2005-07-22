OSTYPE := $(shell uname -s | tr '[A-Z]' '[a-z]')

ifeq ($(OSTYPE),hpux)
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
ifeq ($(OSTYPE),darwin)
	EXTRATOOLS=darwin-meminfo
endif

all: hobbitlaunch bb hobbitclient.cfg clientlaunch.cfg hobbitclient.cfg $(EXTRATOOLS)

hobbitclient.cfg: hobbitclient.cfg.DIST
	cat hobbitclient.cfg.DIST | sed -e's!@BBHOME@!$(BBHOME)!g' | sed -e's!@BBHOSTIP@!$(BBHOSTIP)!g' >hobbitclient.cfg
	../build/bb-commands.sh >>hobbitclient.cfg

clientlaunch.cfg: clientlaunch.cfg.DIST
	cat clientlaunch.cfg.DIST | sed -e's!@BBHOME@!$(BBHOME)!g' >clientlaunch.cfg

hpux-meminfo: hpux-meminfo.c
	$(CC) -o $@ hpux-meminfo.c

bsd-meminfo: bsd-meminfo.c
	$(CC) -o $@ bsd-meminfo.c

install:
	if test ! -d $(INSTALLROOT)$(BBHOME)     ; then mkdir -p $(INSTALLROOT)$(BBHOME)     ; fi
	if test ! -d $(INSTALLROOT)$(BBHOME)/bin ; then mkdir -p $(INSTALLROOT)$(BBHOME)/bin ; fi
	if test ! -d $(INSTALLROOT)$(BBHOME)/etc ; then mkdir -p $(INSTALLROOT)$(BBHOME)/etc ; fi
	if test ! -d $(INSTALLROOT)$(BBHOME)/tmp ; then mkdir -p $(INSTALLROOT)$(BBHOME)/tmp ; fi
	if test ! -d $(INSTALLROOT)$(BBHOME)/logs; then mkdir -p $(INSTALLROOT)$(BBHOME)/logs; fi
	cp -p runclient.sh $(INSTALLROOT)$(BBHOME)
	cp -p hobbitlaunch bb hobbitclient*.sh $(EXTRATOOLS) $(INSTALLROOT)$(BBHOME)/bin/
	../build/merge-sects clientlaunch.cfg $(INSTALLROOT)$(BBHOME)/etc/clientlaunch.cfg
	../build/merge-lines hobbitclient.cfg $(INSTALLROOT)$(BBHOME)/etc/hobbitclient.cfg
	chown -R $(BBUSER) $(INSTALLROOT)$(BBHOME)

clean:
	rm -f bb hobbitlaunch bb clientlaunch.cfg hobbitclient.cfg *-meminfo *~

