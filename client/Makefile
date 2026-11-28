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

# What xymonclient.cfg names for XYMONCLIENTRUNDIR, where msgcache writes its
# pidfile. A build that set XYMONCLIENTRUNDIR names that directory outright; a
# default build keeps the relocatable "$XYMONCLIENTLOGS" -- the client's log
# directory, where msgcache.pid has always been -- resolved when the client
# starts, so the tarball still moves. $$XYMONCLIENTLOGS reaches the sed
# replacement as the literal $XYMONCLIENTLOGS.
ifeq ($(strip $(XYMONCLIENTRUNDIR)),)
XYMONCLIENTRUNDIRSUB = $$XYMONCLIENTLOGS
else
XYMONCLIENTRUNDIRSUB = $(XYMONCLIENTRUNDIR)
endif

xymonclient.cfg: xymonclient.cfg.DIST
	XYMONHOSTIP='$(XYMONHOSTIP)' XYMONHOME='$(XYMONHOME)' XYMONCLIENTRUNDIR='$(XYMONCLIENTRUNDIRSUB)' ../build/subst.sh XYMONHOSTIP XYMONHOME XYMONCLIENTRUNDIR <xymonclient.cfg.DIST >xymonclient.cfg
	../build/bb-commands.sh >>xymonclient.cfg

clientlaunch.cfg: clientlaunch.cfg.DIST
ifeq ($(LOCALCLIENT),yes)
	CLIENTFLAGS='--local' XYMONHOME='$(XYMONHOME)' ../build/subst.sh CLIENTFLAGS XYMONHOME <clientlaunch.cfg.DIST >clientlaunch.cfg
else
	CLIENTFLAGS='' XYMONHOME='$(XYMONHOME)' ../build/subst.sh CLIENTFLAGS XYMONHOME <clientlaunch.cfg.DIST >clientlaunch.cfg
endif

logfetch: logfetch.c $(XYMONCLIENTLIB)
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ logfetch.c $(XYMONCLIENTLIBS)

clientupdate: clientupdate.c $(XYMONCLIENTCOMMLIB) $(XYMONCLIENTLIB)
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ clientupdate.c $(XYMONCLIENTCOMMLIBS) $(XYMONCLIENTLIBS)

orcaxymon: orcaxymon.c $(XYMONCLIENTCOMMLIB) $(XYMONCLIENTLIB)
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ orcaxymon.c $(XYMONCLIENTCOMMLIBS) $(XYMONCLIENTLIBS)

msgcache: msgcache.c $(XYMONCLIENTLIB)
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ msgcache.c $(XYMONCLIENTCOMMLIBS) $(XYMONCLIENTLIBS)

hpux-meminfo: hpux-meminfo.c
	$(CC) $(LDFLAGS) -o $@ hpux-meminfo.c

freebsd-meminfo: freebsd-meminfo.c
	$(CC) $(LDFLAGS) -o $@ freebsd-meminfo.c

openbsd-meminfo: openbsd-meminfo.c
	$(CC) $(LDFLAGS) -o $@ openbsd-meminfo.c

netbsd-meminfo: netbsd-meminfo.c
	$(CC) $(LDFLAGS) -o $@ netbsd-meminfo.c

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

# install-safe places every file and directory without following a symlink at
# the destination name, owning and moding each as it goes (-o/-g below), so the
# tree is Xymon-owned at creation: no recursive chown pass, and nothing the
# unprivileged user left in the install directory is followed.
INSTALLSAFE = ../build/install-safe
ifndef PKGBUILD
ISOWN = -o $(XYMONUSER) -g `$(IDTOOL) -g $(XYMONUSER)`
endif

install:
	$(INSTALLSAFE) mkdir -m 755 $(ISOWN) $(INSTALLROOT)$(XYMONHOME)
	$(INSTALLSAFE) mkdir -m 755 $(ISOWN) $(INSTALLROOT)$(INSTALLCLIENTBINDIR)
	$(INSTALLSAFE) mkdir -m 755 $(ISOWN) $(INSTALLROOT)$(INSTALLCLIENTETCDIR)
	$(INSTALLSAFE) mkdir -m 755 $(ISOWN) $(INSTALLROOT)$(INSTALLCLIENTTMPDIR)
	$(INSTALLSAFE) mkdir -m 755 $(ISOWN) $(INSTALLROOT)$(INSTALLCLIENTLOGDIR)
	$(INSTALLSAFE) mkdir -m 755 $(ISOWN) $(INSTALLROOT)$(XYMONHOME)/ext
	$(INSTALLSAFE) mkdir -m 755 $(ISOWN) $(INSTALLROOT)$(XYMONHOME)/local
	# The drop-in directories clientlaunch.cfg and xymonclient.cfg include,
	# made here as xymond/Makefile makes the server's own.
	for d in clientlaunch.d xymonclient.d; do $(INSTALLSAFE) mkdir -m 755 $(ISOWN) $(INSTALLROOT)$(INSTALLCLIENTETCDIR)/$$d || exit 1; done
	if test ! -f $(INSTALLROOT)$(INSTALLCLIENTETCDIR)/localclient.cfg ; then $(INSTALLSAFE) copy -m 644 $(ISOWN) localclient.cfg $(INSTALLROOT)$(INSTALLCLIENTETCDIR)/localclient.cfg; fi
	if test ! -f $(INSTALLROOT)$(XYMONHOME)/local/README; then $(INSTALLSAFE) copy -m 644 $(ISOWN) README-local $(INSTALLROOT)$(XYMONHOME)/local/README; fi
	$(INSTALLSAFE) copy -m 755 $(ISOWN) runclient.sh $(INSTALLROOT)$(XYMONHOME)/runclient.sh
	for f in $(PROGRAMS) xymonclient*.sh $(COMMONTOOLS) $(EXTRATOOLS); do $(INSTALLSAFE) copy -m 755 $(ISOWN) $$f $(INSTALLROOT)$(INSTALLCLIENTBINDIR)/$$f || exit 1; done

	# The client .cfg are installed over the old copy on every upgrade;
	# customisation lives in clientlaunch.d / xymonclient.d. NOTE: this resets
	# XYMSRV in xymonclient.cfg to its default (the old copy is kept as .bak) --
	# an existing client must carry its server address in xymonclient.d/. See
	# releasenotes.d.
	for f in clientlaunch.cfg xymonclient.cfg; do \
		d="$(INSTALLROOT)$(INSTALLCLIENTETCDIR)/$$f"; \
		if [ -f "$$d" ] && [ ! -L "$$d" ]; then $(INSTALLSAFE) copy -m 600 "$$d" "$$d.bak" || exit 1; fi; \
		$(INSTALLSAFE) copy -m 644 $(ISOWN) $$f "$$d" || exit 1; \
	done

install-localclient:
	$(INSTALLSAFE) copy -m 755 $(ISOWN) xymond_client $(INSTALLROOT)$(INSTALLCLIENTBINDIR)/xymond_client

clean:
	rm -f $(PROGRAMS) $(COMMONTOOLS) xymond_client xymonclient.cfg clientlaunch.cfg $(EXTRATOOLS) *~

