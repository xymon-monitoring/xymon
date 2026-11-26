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
else
   APACHECONF = etcfiles/xymon-apache-secure.DIST
endif

# The Apache alias for the web pages. A literal Alias, so the URL is never
# read as a regular expression; at the root ("/" to configure leaves
# XYMONHOSTURL empty) it needs the trailing slash on both sides.
XYMONWEBALIAS = $(if $(XYMONHOSTURL),Alias $(XYMONHOSTURL) "$(INSTALLWWWDIR)",Alias / "$(INSTALLWWWDIR)/")

cfgfiles:
	XYMONHOME='$(XYMONHOME)' INSTALLETCDIR='$(INSTALLETCDIR)' INSTALLWWWDIR='$(INSTALLWWWDIR)' CGIDIR='$(CGIDIR)' SECURECGIDIR='$(SECURECGIDIR)' XYMONHOSTURL='$(XYMONHOSTURL)' XYMONCGIURL='$(XYMONCGIURL)' SECUREXYMONCGIURL='$(SECUREXYMONCGIURL)' XYMONWEBALIAS='$(XYMONWEBALIAS)' ../build/subst.sh XYMONHOME INSTALLETCDIR INSTALLWWWDIR CGIDIR SECURECGIDIR XYMONHOSTURL XYMONCGIURL SECUREXYMONCGIURL XYMONWEBALIAS <$(APACHECONF) >etcfiles/xymon-apache.conf
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
	rm -f etcfiles/xymon-apache.conf etcfiles/xymonserver.cfg etcfiles/hosts.cfg etcfiles/alerts.cfg etcfiles/tasks.cfg etcfiles/cgioptions.cfg etcfiles/analysis.cfg etcfiles/combo.cfg etcfiles/graphs.cfg etcfiles/client-local.cfg etcfiles/rrddefinitions.cfg
	(find etcfiles/ -name "*~"; find wwwfiles/ -name "*~"; find webfiles/ -name "*~") | xargs rm -f

install: install-bin install-man install-cfg

install-bin:
ifndef PKGBUILD
	chown -h $(XYMONUSER) $(PROGRAMS)
	chgrp -h `$(IDTOOL) -g $(XYMONUSER)` $(PROGRAMS)
	chmod 755 $(PROGRAMS)
endif
	for f in $(PROGRAMS); do rm -f $(INSTALLROOT)$(INSTALLBINDIR)/$$f; done
	cp -fp $(PROGRAMS) $(INSTALLROOT)$(INSTALLBINDIR)/

install-man:
ifndef PKGBUILD
	chown -h $(XYMONUSER) *.1 *.5 *.8
	chgrp -h `$(IDTOOL) -g $(XYMONUSER)` *.1 *.5 *.8
	chmod 644 *.1 *.5 *.8
endif
	mkdir -p $(INSTALLROOT)$(MANROOT)/man1 $(INSTALLROOT)$(MANROOT)/man5 $(INSTALLROOT)$(MANROOT)/man8
ifndef PKGBUILD
	chown -h $(XYMONUSER) $(INSTALLROOT)$(MANROOT)/man1 $(INSTALLROOT)$(MANROOT)/man5 $(INSTALLROOT)$(MANROOT)/man8
	chgrp -h `$(IDTOOL) -g $(XYMONUSER)` $(INSTALLROOT)$(MANROOT)/man1 $(INSTALLROOT)$(MANROOT)/man5 $(INSTALLROOT)$(MANROOT)/man8
	chmod 755 $(INSTALLROOT)$(MANROOT) $(INSTALLROOT)$(MANROOT)/man1 $(INSTALLROOT)$(MANROOT)/man5 $(INSTALLROOT)$(MANROOT)/man8
endif
	for m in *.1; do rm -f $(INSTALLROOT)$(MANROOT)/man1/$$m; done
	cp -fp *.1 $(INSTALLROOT)$(MANROOT)/man1/
	for m in *.5; do rm -f $(INSTALLROOT)$(MANROOT)/man5/$$m; done
	cp -fp *.5 $(INSTALLROOT)$(MANROOT)/man5/
	for m in *.8; do rm -f $(INSTALLROOT)$(MANROOT)/man8/$$m; done
	cp -fp *.8 $(INSTALLROOT)$(MANROOT)/man8/

install-cfg:
	cd etcfiles; if test -L $(INSTALLROOT)$(INSTALLETCDIR)/xymonserver.cfg; then if test -f $(INSTALLROOT)$(INSTALLETCDIR)/xymonserver.cfg; then exit 0; fi; rm -f $(INSTALLROOT)$(INSTALLETCDIR)/xymonserver.cfg; fi; ../../build/merge-lines xymonserver.cfg $(INSTALLROOT)$(INSTALLETCDIR)/xymonserver.cfg LARRDCOLUMN=TRENDSCOLUMN LARRDS=TEST2RRD
	cd etcfiles; if test -L $(INSTALLROOT)$(INSTALLETCDIR)/cgioptions.cfg; then if test -f $(INSTALLROOT)$(INSTALLETCDIR)/cgioptions.cfg; then exit 0; fi; rm -f $(INSTALLROOT)$(INSTALLETCDIR)/cgioptions.cfg; fi; ../../build/merge-lines cgioptions.cfg $(INSTALLROOT)$(INSTALLETCDIR)/cgioptions.cfg
	cd etcfiles; if test -L $(INSTALLROOT)$(INSTALLETCDIR)/tasks.cfg; then if test -f $(INSTALLROOT)$(INSTALLETCDIR)/tasks.cfg; then exit 0; fi; rm -f $(INSTALLROOT)$(INSTALLETCDIR)/tasks.cfg; fi; ../../build/merge-sects tasks.cfg $(INSTALLROOT)$(INSTALLETCDIR)/tasks.cfg larrdstatus=rrdstatus larrddata=rrddata
	cd etcfiles; if test -L $(INSTALLROOT)$(INSTALLETCDIR)/client-local.cfg; then if test -f $(INSTALLROOT)$(INSTALLETCDIR)/client-local.cfg; then exit 0; fi; rm -f $(INSTALLROOT)$(INSTALLETCDIR)/client-local.cfg; fi; ../../build/merge-sects client-local.cfg $(INSTALLROOT)$(INSTALLETCDIR)/client-local.cfg
	cd etcfiles; if test -L $(INSTALLROOT)$(INSTALLETCDIR)/graphs.cfg; then if test -f $(INSTALLROOT)$(INSTALLETCDIR)/graphs.cfg; then exit 0; fi; rm -f $(INSTALLROOT)$(INSTALLETCDIR)/graphs.cfg; fi; ../../build/merge-sects graphs.cfg $(INSTALLROOT)$(INSTALLETCDIR)/graphs.cfg
	cd etcfiles; if test -L $(INSTALLROOT)$(INSTALLETCDIR)/columndoc.csv; then if test -f $(INSTALLROOT)$(INSTALLETCDIR)/columndoc.csv; then exit 0; fi; rm -f $(INSTALLROOT)$(INSTALLETCDIR)/columndoc.csv; fi; ../../build/merge-lines columndoc.csv $(INSTALLROOT)$(INSTALLETCDIR)/columndoc.csv
	cd etcfiles; (echo "hosts.cfg"; echo "alerts.cfg"; echo "analysis.cfg"; echo "combo.cfg"; echo "client-local.cfg"; echo "holidays.cfg"; echo "rrddefinitions.cfg"; echo snmpmibs.cfg; echo xymonmenu.cfg; echo xymon-apache.conf) | ../../build/setup-newfiles $(INSTALLROOT)$(INSTALLETCDIR)/
	cd $(INSTALLROOT)$(XYMONHOME); rm -f xymon.sh; ln -sf bin/xymon.sh .
	cd wwwfiles; find . | grep -v RCS | grep -v ".svn" | grep -v DIST | ../../build/setup-newfiles $(INSTALLROOT)$(INSTALLSTATICWWWDIR)/ ../../build/md5.dat
	cd webfiles; find . | grep -v RCS | grep -v ".svn" | grep -v DIST | ../../build/setup-newfiles $(INSTALLROOT)$(INSTALLWEBDIR)/ ../../build/md5.dat
	# critical.cfg/.bak are state files xymond maintains; an admin may legitimately
	# symlink them to persistent storage, so do not remove a symlink here. touch
	# ensures they exist; skip the chmod when the name is a symlink rather than
	# follow it to set the target's mode.
	for c in $(INSTALLROOT)$(INSTALLETCDIR)/critical.cfg $(INSTALLROOT)$(INSTALLETCDIR)/critical.cfg.bak; do if test ! -L $$c; then touch $$c && chmod 664 $$c || exit 1; fi; done
	mkdir -p $(INSTALLROOT)$(XYMONLOGDIR); test -L $(INSTALLROOT)$(XYMONLOGDIR) || chmod 755 $(INSTALLROOT)$(XYMONLOGDIR)
	mkdir -p $(INSTALLROOT)$(XYMONRUNDIR); test -L $(INSTALLROOT)$(XYMONRUNDIR) || chmod 755 $(INSTALLROOT)$(XYMONRUNDIR)
	mkdir -p $(INSTALLROOT)$(INSTALLETCDIR)/tasks.d; test -L $(INSTALLROOT)$(INSTALLETCDIR)/tasks.d || chmod 755 $(INSTALLROOT)$(INSTALLETCDIR)/tasks.d
	for d in alerts.d analysis.d combo.d graphs.d client-local.d rrddefinitions.d hosts.d xymonserver.d; do mkdir -p $(INSTALLROOT)$(INSTALLETCDIR)/$$d; test -L $(INSTALLROOT)$(INSTALLETCDIR)/$$d || chmod 755 $(INSTALLROOT)$(INSTALLETCDIR)/$$d || exit 1; done
ifndef PKGBUILD
	chown -h $(XYMONUSER) $(INSTALLROOT)$(XYMONLOGDIR) $(INSTALLROOT)$(XYMONRUNDIR) $(INSTALLROOT)$(XYMONHOME) $(INSTALLROOT)$(XYMONHOME)/* $(INSTALLROOT)$(INSTALLSTATICWWWDIR)/gifs $(INSTALLROOT)$(INSTALLSTATICWWWDIR)/menu $(INSTALLROOT)$(INSTALLSTATICWWWDIR)/help $(INSTALLROOT)$(INSTALLWWWDIR)/notes $(INSTALLROOT)$(INSTALLWWWDIR)/html $(INSTALLROOT)$(XYMONVAR)
	chgrp -h `$(IDTOOL) -g $(XYMONUSER)` $(INSTALLROOT)$(XYMONLOGDIR) $(INSTALLROOT)$(XYMONRUNDIR) $(INSTALLROOT)$(XYMONHOME) $(INSTALLROOT)$(XYMONHOME)/* $(INSTALLROOT)$(INSTALLSTATICWWWDIR)/gifs $(INSTALLROOT)$(INSTALLSTATICWWWDIR)/menu $(INSTALLROOT)$(INSTALLSTATICWWWDIR)/help $(INSTALLROOT)$(INSTALLWWWDIR)/notes $(INSTALLROOT)$(INSTALLWWWDIR)/html $(INSTALLROOT)$(XYMONVAR)
	# chown -h protects only the final component, so a glob "$(INSTALLBINDIR)/*"
	# would follow a symlink $(XYMONUSER) may have left at bin/etc/web/... to
	# its target and re-own the files there. find given a directory as its start
	# path acts on a symlink at that path without descending it, so recurse each
	# of these directories rather than glob into it.
	for d in $(INSTALLROOT)$(INSTALLBINDIR) $(INSTALLROOT)$(INSTALLETCDIR) $(INSTALLROOT)$(INSTALLWEBDIR) $(INSTALLROOT)$(INSTALLSTATICWWWDIR)/gifs $(INSTALLROOT)$(INSTALLSTATICWWWDIR)/menu $(INSTALLROOT)$(XYMONVAR); do \
		find $$d -exec chown -h $(XYMONUSER) {} \; ; \
		find $$d -exec chgrp -h `$(IDTOOL) -g $(XYMONUSER)` {} \; ; \
	done
	# ext/ holds the network-test helpers; a server built without xymonnet
	# leaves it empty, and a bare "$(INSTALLEXTDIR)/*" then reaches chown -h as a
	# literal unmatched path and fails the install. Recurse over the directory,
	# which is well-defined whether it has contents or not.
	find $(INSTALLROOT)$(INSTALLEXTDIR) -exec chown -h $(XYMONUSER) {} \;
	find $(INSTALLROOT)$(INSTALLEXTDIR) -exec chgrp -h `$(IDTOOL) -g $(XYMONUSER)` {} \;
	chgrp -h $(HTTPDGID) $(INSTALLROOT)$(INSTALLETCDIR)/critical.cfg $(INSTALLROOT)$(INSTALLETCDIR)/critical.cfg.bak
	for c in $(INSTALLROOT)$(INSTALLETCDIR)/critical.cfg $(INSTALLROOT)$(INSTALLETCDIR)/critical.cfg.bak; do test -L $$c || chmod 664 $$c || exit 1; done

	chown -h root $(INSTALLROOT)$(INSTALLBINDIR)/xymonping || echo "Could not make xymonping owned by root, continuing"
	# chmod has no portable -h and this sets the setuid bit, so never run it on a
	# symlink left at the name (xymonnet installs the real binary, in a separate
	# target that need not run first): skip it when the name is a symlink.
	test -L $(INSTALLROOT)$(INSTALLBINDIR)/xymonping || chmod 4755 $(INSTALLROOT)$(INSTALLBINDIR)/xymonping || echo "Could not make xymonping suid-root, continuing"
endif

