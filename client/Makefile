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

install:
	if test ! -d $(INSTALLROOT)$(XYMONHOME)       ; then mkdir -p $(INSTALLROOT)$(XYMONHOME)       ; test -L $(INSTALLROOT)$(XYMONHOME) || chmod 755 $(INSTALLROOT)$(XYMONHOME)      ; fi
	if test ! -d $(INSTALLROOT)$(INSTALLCLIENTBINDIR)   ; then mkdir -p $(INSTALLROOT)$(INSTALLCLIENTBINDIR)   ; test -L $(INSTALLROOT)$(INSTALLCLIENTBINDIR) || chmod 755 $(INSTALLROOT)$(INSTALLCLIENTBINDIR)  ; fi
	if test ! -d $(INSTALLROOT)$(INSTALLCLIENTETCDIR)   ; then mkdir -p $(INSTALLROOT)$(INSTALLCLIENTETCDIR)   ; test -L $(INSTALLROOT)$(INSTALLCLIENTETCDIR) || chmod 755 $(INSTALLROOT)$(INSTALLCLIENTETCDIR)  ; fi
	if test ! -d $(INSTALLROOT)$(INSTALLCLIENTTMPDIR)   ; then mkdir -p $(INSTALLROOT)$(INSTALLCLIENTTMPDIR)   ; test -L $(INSTALLROOT)$(INSTALLCLIENTTMPDIR) || chmod 755 $(INSTALLROOT)$(INSTALLCLIENTTMPDIR)  ; fi
	if test ! -d $(INSTALLROOT)$(INSTALLCLIENTLOGDIR)  ; then mkdir -p $(INSTALLROOT)$(INSTALLCLIENTLOGDIR)  ; test -L $(INSTALLROOT)$(INSTALLCLIENTLOGDIR) || chmod 755 $(INSTALLROOT)$(INSTALLCLIENTLOGDIR) ; fi
	if test ! -d $(INSTALLROOT)$(XYMONHOME)/ext   ; then mkdir -p $(INSTALLROOT)$(XYMONHOME)/ext   ; test -L $(INSTALLROOT)$(XYMONHOME)/ext || chmod 755 $(INSTALLROOT)$(XYMONHOME)/ext  ; fi
	if test ! -d $(INSTALLROOT)$(XYMONHOME)/local ; then mkdir -p $(INSTALLROOT)$(XYMONHOME)/local ; test -L $(INSTALLROOT)$(XYMONHOME)/local || chmod 755 $(INSTALLROOT)$(XYMONHOME)/local; fi
	# The drop-in directories clientlaunch.cfg and xymonclient.cfg include,
	# made here as xymond/Makefile makes the server's own.
	for d in clientlaunch.d xymonclient.d; do mkdir -p $(INSTALLROOT)$(INSTALLCLIENTETCDIR)/$$d; test -L $(INSTALLROOT)$(INSTALLCLIENTETCDIR)/$$d || chmod 755 $(INSTALLROOT)$(INSTALLCLIENTETCDIR)/$$d || exit 1; done
	# These install a default only when the name is absent, to keep an admin's
	# edited copy on reinstall. A symlink left at the name would make "test ! -f"
	# follow it and cp write through it, so remove a symlink there first.
	if test -L $(INSTALLROOT)$(INSTALLCLIENTETCDIR)/localclient.cfg && test ! -f $(INSTALLROOT)$(INSTALLCLIENTETCDIR)/localclient.cfg; then rm -f $(INSTALLROOT)$(INSTALLCLIENTETCDIR)/localclient.cfg; fi; if test ! -f $(INSTALLROOT)$(INSTALLCLIENTETCDIR)/localclient.cfg ; then cp localclient.cfg $(INSTALLROOT)$(INSTALLCLIENTETCDIR)/ ; chmod 644 $(INSTALLROOT)$(INSTALLCLIENTETCDIR)/localclient.cfg; fi
	if test -L $(INSTALLROOT)$(XYMONHOME)/local/README && test ! -f $(INSTALLROOT)$(XYMONHOME)/local/README; then rm -f $(INSTALLROOT)$(XYMONHOME)/local/README; fi; if test ! -f $(INSTALLROOT)$(XYMONHOME)/local/README; then cp README-local $(INSTALLROOT)$(XYMONHOME)/local/README ; chmod 644 $(INSTALLROOT)$(XYMONHOME)/local/README; fi
	chmod 755 runclient.sh $(PROGRAMS) xymonclient*.sh $(COMMONTOOLS) $(EXTRATOOLS)
	# Remove each destination name before copying: an earlier install, or #411's
	# relocation, may have left a symlink at the name, and cp -fp would write
	# through it to the file it points to. rm -f removes a symlink (never the file
	# it points to, never a real file's content); cp then creates a fresh file.
	rm -f $(INSTALLROOT)$(XYMONHOME)/runclient.sh
	cp -fp runclient.sh $(INSTALLROOT)$(XYMONHOME)
	for f in $(PROGRAMS) xymonclient*.sh $(COMMONTOOLS) $(EXTRATOOLS); do rm -f $(INSTALLROOT)$(INSTALLCLIENTBINDIR)/$$f; done
	cp -fp $(PROGRAMS) xymonclient*.sh $(COMMONTOOLS) $(EXTRATOOLS) $(INSTALLROOT)$(INSTALLCLIENTBINDIR)/

	../build/merge-sects clientlaunch.cfg $(INSTALLROOT)$(INSTALLCLIENTETCDIR)/clientlaunch.cfg
	../build/merge-lines xymonclient.cfg $(INSTALLROOT)$(INSTALLCLIENTETCDIR)/xymonclient.cfg
	chmod 644 $(INSTALLROOT)$(INSTALLCLIENTETCDIR)/clientlaunch.cfg $(INSTALLROOT)$(INSTALLCLIENTETCDIR)/xymonclient.cfg

ifndef PKGBUILD
	# Own the tree with find, not chown -hR: the BSDs reject -h together with
	# -R, and a plain -R would, on Solaris, AIX and HP-UX, change the directory
	# a relocated bin or etc symlink points to. find walks without following a
	# symlink, and chown -h on each entry acts on the link, not its target.
	find $(INSTALLROOT)$(XYMONHOME) -exec chown -h $(XYMONUSER) {} \;
	find $(INSTALLROOT)$(XYMONHOME) -exec chgrp -h `$(IDTOOL) -g $(XYMONUSER)` {} \;
	test -L $(INSTALLROOT)$(XYMONHOME) || chmod 755 $(INSTALLROOT)$(XYMONHOME)
	# A relocated bin or etc is outside XYMONHOME: the tools keep whatever
	# ownership cp preserved, and the config files and drop-in directories are
	# created here and stay root's. Named one by one rather than chowned
	# recursively -
	# a relocated etc may be shared with the rest of the system, where
	# nothing else is ours to touch. A no-op in the default layout, where the
	# recursion already covered them.
	for f in $(PROGRAMS) xymonclient*.sh $(COMMONTOOLS) $(EXTRATOOLS); do \
		chown -h $(XYMONUSER) $(INSTALLROOT)$(INSTALLCLIENTBINDIR)/$$f ; \
		chgrp -h `$(IDTOOL) -g $(XYMONUSER)` $(INSTALLROOT)$(INSTALLCLIENTBINDIR)/$$f ; \
	done
	for f in localclient.cfg clientlaunch.cfg xymonclient.cfg clientlaunch.d xymonclient.d; do \
		chown -h $(XYMONUSER) $(INSTALLROOT)$(INSTALLCLIENTETCDIR)/$$f ; \
		chgrp -h `$(IDTOOL) -g $(XYMONUSER)` $(INSTALLROOT)$(INSTALLCLIENTETCDIR)/$$f ; \
	done
endif

install-localclient:
	chmod 755 xymond_client
	rm -f $(INSTALLROOT)$(INSTALLCLIENTBINDIR)/xymond_client
	cp -fp xymond_client $(INSTALLROOT)$(INSTALLCLIENTBINDIR)/
ifndef PKGBUILD
	chown -h $(XYMONUSER) $(INSTALLROOT)$(INSTALLCLIENTBINDIR)/xymond_client
	chgrp -h `$(IDTOOL) -g $(XYMONUSER)` $(INSTALLROOT)$(INSTALLCLIENTBINDIR)/xymond_client
endif

clean:
	rm -f $(PROGRAMS) $(COMMONTOOLS) xymond_client xymonclient.cfg clientlaunch.cfg $(EXTRATOOLS) *~

