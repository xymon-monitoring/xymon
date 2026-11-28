# build/Makefile.rules sets this; the default here is for a "make -C" of
# this directory alone, which would otherwise install the web content at /.
INSTALLSTATICWWWDIR ?= $(INSTALLWWWDIR)

XYMONLIB = ../lib/libxymon.a
XYMONLIBS = $(XYMONLIB)
XYMONCOMMLIB = ../lib/libxymoncomm.a
XYMONCOMMLIBS = $(XYMONCOMMLIB) $(ZLIBLIBS) $(SSLLIBS) $(NETLIBS) $(LIBRTDEF)
XYMONTIMELIB = ../lib/libxymontime.a
XYMONTIMELIBS = $(XYMONTIMELIB) $(LIBRTDEF)

PROGRAMS = xymon.sh xymond xymond_channel xymond_locator xymond_filestore xymond_history xymond_alert xymond_sample xymond_client xymond_hostdata xymond_capture xymond_distribute xymonfetch xymon-mailack trimhistory combostatus xymonreports.sh moverrd.sh convertnk rrdcachectl
CLIENTPROGRAMS = ../client/xymond_client

ifeq ($(DORRD),yes)
	PROGRAMS += xymond_rrd
endif

XYMONDOBJS    = xymond.o
CHANNELOBJS   = xymond_channel.o
LOCATOROBJS   = xymond_locator.o
SAMPLEOBJS    = xymond_sample.o    xymond_worker.o
FILESTOREOBJS = xymond_filestore.o xymond_worker.o
HISTORYOBJS   = xymond_history.o   xymond_worker.o
ALERTOBJS     = xymond_alert.o     xymond_worker.o do_alert.o
RRDOBJS       = xymond_rrd.o       xymond_worker.o do_rrd.o client_config.o
HOSTDATAOBJS  = xymond_hostdata.o  xymond_worker.o
CAPTUREOBJS   = xymond_capture.o   xymond_worker.o
CLIENTOBJS    = xymond_client.o    xymond_worker.o client_config.o
DISTRIBUTEOBJS= xymond_distribute.o xymond_worker.o
COMBOTESTOBJS = combostatus.o
MAILACKOBJS   = xymon-mailack.o
TRIMHISTOBJS  = trimhistory.o
FETCHOBJS     = xymonfetch.o
CONVERTNKOBJS = convertnk.o
RRDCACHECTLOBJS = rrdcachectl.o

IDTOOL := $(shell if test `uname -s` = "SunOS"; then echo /usr/xpg4/bin/id; else echo id; fi)


all: $(PROGRAMS) cfgfiles

# Need NETLIBS on Solaris for getservbyname(), called by parse_url()

client: $(CLIENTPROGRAMS)

xymond: $(XYMONDOBJS) $(XYMONCOMMLIB) $(XYMONTIMELIB)
	$(CC) $(LDFLAGS) -o $@ $(RPATHOPT) $(XYMONDOBJS) $(XYMONCOMMLIBS) $(XYMONTIMELIBS) $(PCRELIBS)

xymond_channel: $(CHANNELOBJS) $(XYMONCOMMLIB) $(XYMONTIMELIB)
	$(CC) $(LDFLAGS) -o $@ $(RPATHOPT) $(CHANNELOBJS) $(XYMONCOMMLIBS) $(XYMONTIMELIBS) $(PCRELIBS)

xymond_locator: $(LOCATOROBJS) $(XYMONCOMMLIB)
	$(CC) $(LDFLAGS) -o $@ $(RPATHOPT) $(LOCATOROBJS) $(XYMONCOMMLIBS)

xymond_filestore: $(FILESTOREOBJS) $(XYMONCOMMLIB) $(XYMONTIMELIB)
	$(CC) $(LDFLAGS) -o $@ $(RPATHOPT) $(FILESTOREOBJS) $(XYMONTIMELIBS) $(XYMONCOMMLIBS) $(PCRELIBS)

xymond_history: $(HISTORYOBJS) $(XYMONCOMMLIB) $(XYMONTIMELIB)
	$(CC) $(LDFLAGS) -o $@ $(RPATHOPT) $(HISTORYOBJS) $(XYMONTIMELIBS) $(XYMONCOMMLIBS)

xymond_alert: $(ALERTOBJS) $(XYMONCOMMLIB)
	$(CC) $(LDFLAGS) -o $@ $(RPATHOPT) $(ALERTOBJS) $(XYMONTIMELIBS) $(XYMONCOMMLIBS) $(PCRELIBS)

# -lm: do_net.c uses isfinite(), which glibc inlines as a compiler builtin but
# the BSDs resolve to __isfinite in libm (issue #232). Harmless where unneeded.
xymond_rrd: $(RRDOBJS) $(XYMONCOMMLIB)
	$(CC) $(LDFLAGS) -o $@ $(RPATHOPT) $(RRDOBJS) $(XYMONTIMELIBS) $(XYMONCOMMLIBS) $(RRDLIBS) $(PCRELIBS) -lm

xymond.o: xymond.c ../lib/xymontls.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SSLFLAGS) $(SSLINCDIR) -c -o $@ xymond.c

do_alert.o: do_alert.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(PCREINCDIR) -c -o $@ do_alert.c

do_rrd.o: do_rrd.c do_rrd.h rrd/*.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(RRDINCDIR) $(PCREINCDIR) $(RRDDEF) -c -o $@ do_rrd.c

xymond_capture.o: xymond_capture.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(PCREINCDIR) -c -o $@ xymond_capture.c

xymond_sample: $(SAMPLEOBJS) $(XYMONCOMMLIB)
	$(CC) $(LDFLAGS) -o $@ $(RPATHOPT) $(SAMPLEOBJS) $(XYMONTIMELIBS) $(XYMONCOMMLIBS)

xymond_capture: $(CAPTUREOBJS) $(XYMONCOMMLIB)
	$(CC) $(LDFLAGS) -o $@ $(RPATHOPT) $(CAPTUREOBJS) $(XYMONTIMELIBS) $(XYMONCOMMLIBS) $(PCRELIBS)

xymond_distribute: $(DISTRIBUTEOBJS) $(XYMONCOMMLIB)
	$(CC) $(LDFLAGS) -o $@ $(RPATHOPT) $(DISTRIBUTEOBJS) $(XYMONTIMELIBS) $(XYMONCOMMLIBS) $(PCRELIBS)

xymond_hostdata: $(HOSTDATAOBJS) $(XYMONCOMMLIB)
	$(CC) $(LDFLAGS) -o $@ $(RPATHOPT) $(HOSTDATAOBJS) $(XYMONTIMELIBS) $(XYMONCOMMLIBS)

xymond_client: $(CLIENTOBJS) $(XYMONCOMMLIB)
	$(CC) $(LDFLAGS) -o $@ $(RPATHOPT) $(CLIENTOBJS) $(XYMONTIMELIBS) $(XYMONCOMMLIBS) $(PCRELIBS)

../client/xymond_client: $(CLIENTOBJS) ../lib/libxymonclientcomm.a  ../lib/libxymontime.a ../lib/libxymonclient.a
	$(CC) $(LDFLAGS) -o $@ $(RPATHOPT) $(CLIENTOBJS) ../lib/libxymonclientcomm.a  ../lib/libxymontime.a ../lib/libxymonclient.a $(PCRELIBS) $(SSLLIBS) $(NETLIBS) $(LIBRTDEF)

xymond_client.o: xymond_client.c client/*.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ xymond_client.c

combostatus.o: combostatus.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(PCREINCDIR) -c -o $@ $<

combostatus: $(COMBOTESTOBJS) $(XYMONCOMMLIB) $(XYMONTIMELIB)
	$(CC) $(LDFLAGS) -o $@ $(RPATHOPT) $(COMBOTESTOBJS) $(XYMONTIMELIBS) $(XYMONCOMMLIBS) $(PCRELIBS)

xymon-mailack.o: xymon-mailack.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(PCREINCDIR) -c -o $@ xymon-mailack.c

xymon-mailack: $(MAILACKOBJS) $(XYMONCOMMLIB)
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $(RPATHOPT) $(MAILACKOBJS) $(XYMONCOMMLIBS) $(PCRELIBS)

trimhistory: $(TRIMHISTOBJS) $(XYMONCOMMLIB)
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $(RPATHOPT) $(TRIMHISTOBJS) $(XYMONCOMMLIBS)

xymonfetch: $(FETCHOBJS) $(XYMONCOMMLIB) $(XYMONTIMELIB)
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $(RPATHOPT) $(FETCHOBJS) $(XYMONTIMELIBS) $(XYMONCOMMLIBS)

convertnk: $(CONVERTNKOBJS) $(XYMONCOMMLIB)
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $(RPATHOPT) $(CONVERTNKOBJS) $(XYMONCOMMLIBS)

rrdcachectl: $(RRDCACHECTLOBJS) $(XYMONCOMMLIB)
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $(RPATHOPT) $(RRDCACHECTLOBJS) $(XYMONCOMMLIBS)

xymon.sh: xymon.sh.DIST
	XYMONHOME='$(XYMONHOME)' XYMONLOGDIR='$(XYMONLOGDIR)' XYMONRUNDIR='$(XYMONRUNDIR)' XYMONUSER='$(XYMONUSER)' RUNTIMEDEFS='$(RUNTIMEDEFS)' ../build/subst.sh XYMONHOME XYMONLOGDIR XYMONRUNDIR XYMONUSER RUNTIMEDEFS <$< >$@
	chmod 755 $@

xymonreports.sh: xymonreports.sh.DIST
	XYMONHOME='$(XYMONHOME)' ../build/subst.sh XYMONHOME <$< >$@
	chmod 755 $@

moverrd.sh: moverrd.sh.DIST
	XYMONHOME='$(XYMONHOME)' XYMONVAR='$(XYMONVAR)' ../build/subst.sh XYMONHOME XYMONVAR <$< >$@
	chmod 755 $@

ifeq ($(XYMONCGIURL),$(SECUREXYMONCGIURL))
   APACHECONF = etcfiles/xymon-apache-open.DIST
   LIGHTTPDCONF = etcfiles/xymon-lighttpd-open.conf.DIST
   NGINXCONF = etcfiles/xymon-nginx-open.conf.DIST
   CADDYCONF = etcfiles/xymon-caddy-open.conf.DIST
else
   APACHECONF = etcfiles/xymon-apache-secure.DIST
   LIGHTTPDCONF = etcfiles/xymon-lighttpd-secure.conf.DIST
   NGINXCONF = etcfiles/xymon-nginx-secure.conf.DIST
   CADDYCONF = etcfiles/xymon-caddy-secure.conf.DIST
endif

# The Apache alias for the web pages. A literal Alias, so the URL is never
# read as a regular expression; at the root ("/" to configure leaves
# XYMONHOSTURL empty) it needs the trailing slash on both sides.
XYMONWEBALIAS = $(if $(XYMONHOSTURL),Alias $(XYMONHOSTURL) "$(INSTALLWWWDIR)",Alias / "$(INSTALLWWWDIR)/")

# The lighttpd, nginx and caddy examples redirect a bare XYMONHOSTURL to its
# trailing-slash form. With Xymon at the site root (XYMONHOSTURL empty) there
# is no bare URL to redirect, and the line is left out.
EXAMPLECOMMENT := \#
LIGHTTPDBAREREDIRECT = $(if $(XYMONHOSTURL),url.redirect += ( "^$(XYMONHOSTURL)$$" => "$(XYMONHOSTURL)/" ),$(EXAMPLECOMMENT) Xymon is at the site root: no redirect for a bare URL)
NGINXBAREREDIRECT = $(if $(XYMONHOSTURL),location = $(XYMONHOSTURL) { return 301 $(XYMONHOSTURL)/; },$(EXAMPLECOMMENT) Xymon is at the site root: no redirect for a bare URL)
CADDYBAREREDIRECT = $(if $(XYMONHOSTURL),redir $(XYMONHOSTURL) $(XYMONHOSTURL)/ 301,$(EXAMPLECOMMENT) Xymon is at the site root: no redirect for a bare URL)

# The lighttpd, nginx and caddy examples guard the secure CGI URL, where
# Apache guards the secure CGI directory. With one directory for both, they
# would serve the administration CGIs through XYMONCGIURL without a password.
ifneq ($(XYMONCGIURL),$(SECUREXYMONCGIURL))
ifeq ($(CGIDIR),$(SECURECGIDIR))
$(warning CGIDIR and SECURECGIDIR are the same directory, $(CGIDIR): the lighttpd, nginx and caddy examples would serve the administration CGIs through $(XYMONCGIURL) without a password; use the Apache configuration, or separate directories)
endif
endif

cfgfiles:
	XYMONHOME='$(XYMONHOME)' INSTALLETCDIR='$(INSTALLETCDIR)' INSTALLWWWDIR='$(INSTALLWWWDIR)' CGIDIR='$(CGIDIR)' SECURECGIDIR='$(SECURECGIDIR)' XYMONHOSTURL='$(XYMONHOSTURL)' XYMONCGIURL='$(XYMONCGIURL)' SECUREXYMONCGIURL='$(SECUREXYMONCGIURL)' XYMONWEBALIAS='$(XYMONWEBALIAS)' ../build/subst.sh XYMONHOME INSTALLETCDIR INSTALLWWWDIR CGIDIR SECURECGIDIR XYMONHOSTURL XYMONCGIURL SECUREXYMONCGIURL XYMONWEBALIAS <$(APACHECONF) >etcfiles/xymon-apache.conf
	XYMONHOME='$(XYMONHOME)' INSTALLETCDIR='$(INSTALLETCDIR)' INSTALLWWWDIR='$(INSTALLWWWDIR)' CGIDIR='$(CGIDIR)' SECURECGIDIR='$(SECURECGIDIR)' XYMONHOSTURL='$(XYMONHOSTURL)' XYMONCGIURL='$(XYMONCGIURL)' SECUREXYMONCGIURL='$(SECUREXYMONCGIURL)' LIGHTTPDBAREREDIRECT='$(LIGHTTPDBAREREDIRECT)' ../build/subst.sh XYMONHOME INSTALLETCDIR INSTALLWWWDIR CGIDIR SECURECGIDIR XYMONHOSTURL XYMONCGIURL SECUREXYMONCGIURL LIGHTTPDBAREREDIRECT <$(LIGHTTPDCONF) >etcfiles/xymon-lighttpd.conf
	XYMONHOME='$(XYMONHOME)' INSTALLETCDIR='$(INSTALLETCDIR)' INSTALLWWWDIR='$(INSTALLWWWDIR)' CGIDIR='$(CGIDIR)' SECURECGIDIR='$(SECURECGIDIR)' XYMONHOSTURL='$(XYMONHOSTURL)' XYMONCGIURL='$(XYMONCGIURL)' SECUREXYMONCGIURL='$(SECUREXYMONCGIURL)' NGINXBAREREDIRECT='$(NGINXBAREREDIRECT)' ../build/subst.sh XYMONHOME INSTALLETCDIR INSTALLWWWDIR CGIDIR SECURECGIDIR XYMONHOSTURL XYMONCGIURL SECUREXYMONCGIURL NGINXBAREREDIRECT <$(NGINXCONF) >etcfiles/xymon-nginx.conf
	XYMONHOME='$(XYMONHOME)' INSTALLETCDIR='$(INSTALLETCDIR)' INSTALLWWWDIR='$(INSTALLWWWDIR)' CGIDIR='$(CGIDIR)' SECURECGIDIR='$(SECURECGIDIR)' XYMONHOSTURL='$(XYMONHOSTURL)' XYMONCGIURL='$(XYMONCGIURL)' SECUREXYMONCGIURL='$(SECUREXYMONCGIURL)' CADDYBAREREDIRECT='$(CADDYBAREREDIRECT)' ../build/subst.sh XYMONHOME INSTALLETCDIR INSTALLWWWDIR CGIDIR SECURECGIDIR XYMONHOSTURL XYMONCGIURL SECUREXYMONCGIURL CADDYBAREREDIRECT <$(CADDYCONF) >etcfiles/xymon-caddy.conf
	XYMONTOPDIR='$(XYMONTOPDIR)' XYMONLOGDIR='$(XYMONLOGDIR)' XYMONRUNDIR='$(XYMONRUNDIR)' XYMONHOSTNAME='$(XYMONHOSTNAME)' XYMONHOSTIP='$(XYMONHOSTIP)' XYMONHOSTOS='$(XYMONHOSTOS)' XYMONHOSTURL='$(XYMONHOSTURL)' XYMONCGIURL='$(XYMONCGIURL)' SECUREXYMONCGIURL='$(SECUREXYMONCGIURL)' XYMONHOME='$(XYMONHOME)' XYMONSTATICWWWDIR='$(INSTALLSTATICWWWDIR)' XYMONVAR='$(XYMONVAR)' FPING='$(FPING)' MAILPROGRAM='$(MAILPROGRAM)' RUNTIMEDEFS='$(RUNTIMEDEFS)' ../build/subst.sh XYMONTOPDIR XYMONLOGDIR XYMONRUNDIR XYMONHOSTNAME XYMONHOSTIP XYMONHOSTOS XYMONHOSTURL XYMONCGIURL SECUREXYMONCGIURL XYMONHOME XYMONSTATICWWWDIR XYMONVAR FPING MAILPROGRAM RUNTIMEDEFS <etcfiles/xymonserver.cfg.DIST >etcfiles/xymonserver.cfg
	../build/bb-commands.sh >>etcfiles/xymonserver.cfg
	XYMONHOSTNAME='$(XYMONHOSTNAME)' XYMONHOSTIP='$(XYMONHOSTIP)' XYMONHOME='$(XYMONHOME)' ../build/subst.sh XYMONHOSTNAME XYMONHOSTIP XYMONHOME <etcfiles/hosts.cfg.DIST >etcfiles/hosts.cfg
	XYMONHOSTNAME='$(XYMONHOSTNAME)' XYMONHOSTIP='$(XYMONHOSTIP)' XYMONHOME='$(XYMONHOME)' ../build/subst.sh XYMONHOSTNAME XYMONHOSTIP XYMONHOME <etcfiles/alerts.cfg.DIST >etcfiles/alerts.cfg
	for f in analysis combo graphs client-local rrddefinitions; do XYMONHOME='$(XYMONHOME)' ../build/subst.sh XYMONHOME <etcfiles/$$f.cfg.DIST >etcfiles/$$f.cfg; done
	XYMONHOME='$(XYMONHOME)' XYMONTOPDIR='$(XYMONTOPDIR)' ../build/subst.sh XYMONHOME XYMONTOPDIR <etcfiles/tasks.cfg.DIST >etcfiles/tasks.cfg
	XYMONHOME='$(XYMONHOME)' XYMONHOSTURL='$(XYMONHOSTURL)' ../build/subst.sh XYMONHOME XYMONHOSTURL <etcfiles/cgioptions.cfg.DIST >etcfiles/cgioptions.cfg

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(PROGRAMS) *.o *~ client/*~ rrd/*~
	rm -f etcfiles/xymon-apache.conf etcfiles/xymon-lighttpd.conf etcfiles/xymon-nginx.conf etcfiles/xymon-caddy.conf etcfiles/xymonserver.cfg etcfiles/hosts.cfg etcfiles/alerts.cfg etcfiles/tasks.cfg etcfiles/cgioptions.cfg etcfiles/analysis.cfg etcfiles/combo.cfg etcfiles/graphs.cfg etcfiles/client-local.cfg etcfiles/rrddefinitions.cfg
	(find etcfiles/ -name "*~"; find wwwfiles/ -name "*~"; find webfiles/ -name "*~") | xargs rm -f

# install-safe places each file and directory without following a symlink at
# the destination name, owning and moding it as it goes. See client/Makefile.
# There is no catch-all chown pass: each file is owned as it is placed, which
# also keeps xymonping the setuid-root install xymonnet/Makefile made it.
INSTALLSAFE = ../build/install-safe
ifndef PKGBUILD
ISOWN = -o $(XYMONUSER) -g `$(IDTOOL) -g $(XYMONUSER)`
# critical.cfg is group-writable by the web server group, for the critical-view
# editor CGI.
# critical.cfg is group-writable (664) by the web server's group. Guard the
# HTTPDGID interpolation as REPOWN does: an empty HTTPDGID would leave a bare
# "-g" that swallows the path and fails the install, so fall back to ISOWN.
CRITOWN = $(ISOWN)
ifdef HTTPDGID
CRITOWN = -o $(XYMONUSER) -g $(HTTPDGID)
endif
endif

install: install-bin install-man install-cfg

install-bin:
	for f in $(PROGRAMS); do $(INSTALLSAFE) copy -m 755 $(ISOWN) $$f $(INSTALLROOT)$(INSTALLBINDIR)/$$f || exit 1; done

install-man:
	for d in man1 man5 man8; do $(INSTALLSAFE) mkdir -m 755 $(ISOWN) $(INSTALLROOT)$(MANROOT)/$$d || exit 1; done
	for m in *.1; do $(INSTALLSAFE) copy -m 644 $(ISOWN) $$m $(INSTALLROOT)$(MANROOT)/man1/$$m || exit 1; done
	for m in *.5; do $(INSTALLSAFE) copy -m 644 $(ISOWN) $$m $(INSTALLROOT)$(MANROOT)/man5/$$m || exit 1; done
	for m in *.8; do $(INSTALLSAFE) copy -m 644 $(ISOWN) $$m $(INSTALLROOT)$(MANROOT)/man8/$$m || exit 1; done

install-cfg:
	# The shipped .cfg track the defaults and are installed over the old copy on
	# every upgrade; customisation lives in the matching .d drop-in directory
	# (xymonserver.d, tasks.d, ...), which the install never touches. An existing
	# real config is first copied to <name>.bak (root-owned 0600) so an edit is
	# recoverable; a symlinked (relocated) config is left for install-safe copy to
	# replace by name. See releasenotes.d. cgioptions.cfg and columndoc.csv have
	# no .d and are overwritten too.
	for f in xymonserver.cfg cgioptions.cfg tasks.cfg client-local.cfg graphs.cfg columndoc.csv; do \
		d="$(INSTALLROOT)$(INSTALLETCDIR)/$$f"; \
		if [ -f "$$d" ] && [ ! -L "$$d" ]; then $(INSTALLSAFE) copy -m 600 "$$d" "$$d.bak" || exit 1; fi; \
		$(INSTALLSAFE) copy -m 644 $(ISOWN) etcfiles/$$f "$$d" || exit 1; \
	done
	# setup-newfiles owns each file it places from $$XYMONUSER (skipped under
	# PKGBUILD, or when not run as root), so its outputs are Xymon-owned too.
	cd etcfiles; (echo "hosts.cfg"; echo "alerts.cfg"; echo "analysis.cfg"; echo "combo.cfg"; echo "client-local.cfg"; echo "holidays.cfg"; echo "rrddefinitions.cfg"; echo snmpmibs.cfg; echo xymonmenu.cfg; echo xymon-apache.conf) | XYMONUSER='$(XYMONUSER)' PKGBUILD='$(PKGBUILD)' ../../build/setup-newfiles $(INSTALLROOT)$(INSTALLETCDIR)/
	cd $(INSTALLROOT)$(XYMONHOME); rm -f xymon.sh; ln -sf bin/xymon.sh .
	cd wwwfiles; find . | grep -v RCS | grep -v ".svn" | grep -v DIST | XYMONUSER='$(XYMONUSER)' PKGBUILD='$(PKGBUILD)' ../../build/setup-newfiles $(INSTALLROOT)$(INSTALLSTATICWWWDIR)/ ../../build/md5.dat
	cd webfiles; find . | grep -v RCS | grep -v ".svn" | grep -v DIST | XYMONUSER='$(XYMONUSER)' PKGBUILD='$(PKGBUILD)' ../../build/setup-newfiles $(INSTALLROOT)$(INSTALLWEBDIR)/ ../../build/md5.dat
	$(INSTALLSAFE) touch -m 664 $(CRITOWN) $(INSTALLROOT)$(INSTALLETCDIR)/critical.cfg
	$(INSTALLSAFE) touch -m 664 $(CRITOWN) $(INSTALLROOT)$(INSTALLETCDIR)/critical.cfg.bak
	$(INSTALLSAFE) mkdir -m 755 $(ISOWN) $(INSTALLROOT)$(XYMONLOGDIR)
	$(INSTALLSAFE) mkdir -m 755 $(ISOWN) $(INSTALLROOT)$(XYMONRUNDIR)
	$(INSTALLSAFE) mkdir -m 755 $(ISOWN) $(INSTALLROOT)$(INSTALLETCDIR)/tasks.d
	for d in alerts.d analysis.d combo.d graphs.d client-local.d rrddefinitions.d hosts.d xymonserver.d; do $(INSTALLSAFE) mkdir -m 755 $(ISOWN) $(INSTALLROOT)$(INSTALLETCDIR)/$$d || exit 1; done
	# No catch-all chown pass: each file and directory above was owned as it was
	# placed -- setup-newfiles' files by setup-newfiles, the rest by their
	# copy/mkdir/touch -- which keeps xymonping the setuid-root install xymonnet
	# made it. The ext/ extensions are owned by xymonnet's install-ext, and
	# anything an administrator left in ext/ is their own, not recursively re-owned.

