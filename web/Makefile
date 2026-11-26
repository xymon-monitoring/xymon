XYMONLIB = ../lib/libxymon.a
XYMONLIBS = $(XYMONLIB)
XYMONCOMMLIB = ../lib/libxymoncomm.a
XYMONCOMMLIBS = $(XYMONCOMMLIB) $(SSLLIBS) $(NETLIBS) $(LIBRTDEF)
XYMONTIMELIB = ../lib/libxymontime.a
XYMONTIMELIBS = $(XYMONTIMELIB) $(LIBRTDEF)

PROGRAMS = history.cgi eventlog.cgi report.cgi reportlog.cgi snapshot.cgi findhost.cgi csvinfo.cgi acknowledge.cgi xymonpage datepage.cgi svcstatus.cgi enadis.cgi confreport.cgi criticalview.cgi criticaleditor.cgi ackinfo.cgi statusreport.cgi boilerplate.cgi hostgraphs.cgi ghostlist.cgi notifications.cgi acknowledgements.cgi hostlist.cgi useradm.cgi chpasswd.cgi appfeed.cgi cgiwrap
CGISCRIPTS = history.sh eventlog.sh report.sh reportlog.sh snapshot.sh findhost.sh csvinfo.sh columndoc.sh datepage.sh svcstatus.sh historylog.sh confreport.sh confreport-critical.sh criticalview.sh certreport.sh nongreen.sh hostgraphs.sh ghostlist.sh notifications.sh acknowledgements.sh hostlist.sh topchanges.sh appfeed.sh appfeed-critical.sh
SECCGISCRIPTS = acknowledge.sh enadis.sh criticaleditor.sh ackinfo.sh useradm.sh chpasswd.sh

ifeq ($(DORRD),yes)
	PROGRAMS += showgraph.cgi perfdata.cgi
	CGISCRIPTS += showgraph.sh perfdata.sh
endif

CGIWRAPOBJS     = cgiwrap.o
HISTOBJS        = history.o
EVENTLOGOBJS    = eventlog.o
REPOBJS         = report.o
REPLOGOBJS      = reportlog.o
SNAPOBJS        = snapshot.o
FINDHOSTOBJS    = findhost.o
CSVINFOOBJS     = csvinfo.o
ACKCGIOBJS      = acknowledge.o
WEBPAGEOBJS     = xymonpage.o
DATEPAGEOBJS    = datepage.o
APPFEEDOBJS	= appfeed.o

SHOWGRAPHOBJS   = showgraph.o
SVCSTATUSOBJS   = svcstatus.o svcstatus-info.o svcstatus-trends.o
ENADISOBJS      = enadis.o
CRITVIEWOBJS    = criticalview.o
CRITEDITOBJS    = criticaleditor.o
ACKINFOOBJS     = ackinfo.o
CONFREPOBJS     = confreport.o
STATUSREPOBJS   = statusreport.o
MAILACKOBJS     = xymon-mailack.o
GHOSTOBJS       = ghostlist.o
NOTIFYOBJS      = notifications.o
ACKNOWLEDGEOBJS = acknowledgements.o
HOSTLISTOBJS    = hostlist.o
PERFDATAOBJS	= perfdata.o
USERADMOBJS	= useradm.o
CHPASSWDOBJS	= chpasswd.o

HOSTGRAPHSOBJS  = hostgraphs.o
BOILERPLATEOBJS = boilerplate.o


IDTOOL := $(shell if test `uname -s` = "SunOS"; then echo /usr/xpg4/bin/id; else echo id; fi)


all: $(PROGRAMS)

cgiwrap.o: cgiwrap.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -DXYMONHOME=\"$(XYMONHOME)\" -c -o $@ $<

cgiwrap: $(CGIWRAPOBJS) $(XYMONCOMMLIB)
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $(CGIWRAPOBJS) $(XYMONCOMMLIBS) $(PCRELIBS)

history.cgi: $(HISTOBJS) $(XYMONCOMMLIB)
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $(HISTOBJS) $(XYMONCOMMLIBS) $(PCRELIBS)

eventlog.cgi: $(EVENTLOGOBJS) $(XYMONCOMMLIB)
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $(EVENTLOGOBJS) $(XYMONCOMMLIBS) $(PCRELIBS)

report.cgi: $(REPOBJS) $(XYMONCOMMLIB)
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $(REPOBJS) $(XYMONCOMMLIBS) $(PCRELIBS)

reportlog.cgi: $(REPLOGOBJS) $(XYMONCOMMLIB)
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $(REPLOGOBJS) $(XYMONCOMMLIBS) $(PCRELIBS)

snapshot.cgi: $(SNAPOBJS) $(XYMONCOMMLIB)
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $(SNAPOBJS) $(XYMONCOMMLIBS) $(PCRELIBS)

findhost.cgi: $(FINDHOSTOBJS) $(XYMONCOMMLIB)
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $(FINDHOSTOBJS) $(XYMONCOMMLIBS) $(PCRELIBS)

csvinfo.cgi: $(CSVINFOOBJS) $(XYMONCOMMLIB)
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $(CSVINFOOBJS) $(XYMONCOMMLIBS) $(PCRELIBS)

acknowledge.cgi: $(ACKCGIOBJS) $(XYMONCOMMLIB)
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $(ACKCGIOBJS) $(XYMONCOMMLIBS) $(PCRELIBS)

xymonpage: $(WEBPAGEOBJS) $(XYMONCOMMLIB)
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $(WEBPAGEOBJS) $(XYMONCOMMLIBS) $(PCRELIBS)

datepage.cgi: $(DATEPAGEOBJS) $(XYMONCOMMLIB)
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $(DATEPAGEOBJS) $(XYMONCOMMLIBS) $(PCRELIBS)

showgraph.o: showgraph.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(PCREINCDIR) $(RRDDEF) $(RRDINCDIR) -c -o $@ $<

# Need NETLIBS on Solaris for getservbyname(), called by parse_url()
showgraph.cgi: $(SHOWGRAPHOBJS) $(XYMONCOMMLIB)
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $(RPATHOPT) $(SHOWGRAPHOBJS) $(XYMONCOMMLIBS) $(PCRELIBS) $(RRDLIBS)

svcstatus.cgi: $(SVCSTATUSOBJS) $(XYMONCOMMLIB)
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $(SVCSTATUSOBJS) $(XYMONCOMMLIBS) $(PCRELIBS)

enadis.cgi: $(ENADISOBJS) $(XYMONCOMMLIB)
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $(ENADISOBJS) $(XYMONCOMMLIBS) $(PCRELIBS)

statusreport.cgi: $(STATUSREPOBJS) $(XYMONCOMMLIB)
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $(STATUSREPOBJS) $(XYMONCOMMLIBS) $(PCRELIBS)

confreport.cgi: $(CONFREPOBJS) $(XYMONCOMMLIB)
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $(CONFREPOBJS) $(XYMONCOMMLIBS) $(PCRELIBS)

criticalview.cgi: $(CRITVIEWOBJS) $(XYMONCOMMLIB)
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $(RPATHOPT) $(CRITVIEWOBJS) $(XYMONCOMMLIBS) $(PCRELIBS)

criticalview.o: criticalview.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(PCREINCDIR) -c -o $@ $<

criticaleditor.cgi: $(CRITEDITOBJS) $(XYMONCOMMLIB)
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $(RPATHOPT) $(CRITEDITOBJS) $(XYMONCOMMLIBS) $(PCRELIBS)

ackinfo.cgi: $(ACKINFOOBJS) $(XYMONCOMMLIB)
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $(RPATHOPT) $(ACKINFOOBJS) $(XYMONCOMMLIBS)

boilerplate.cgi: $(BOILERPLATEOBJS) $(XYMONCOMMLIB)
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $(RPATHOPT) $(BOILERPLATEOBJS) $(XYMONCOMMLIBS) $(PCRELIBS)

hostgraphs.cgi: $(HOSTGRAPHSOBJS) $(XYMONCOMMLIB)
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $(RPATHOPT) $(HOSTGRAPHSOBJS) $(XYMONCOMMLIBS) $(PCRELIBS)

ghostlist.cgi: $(GHOSTOBJS) $(XYMONCOMMLIB)
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $(RPATHOPT) $(GHOSTOBJS) $(XYMONCOMMLIBS) $(PCRELIBS)

notifications.cgi: $(NOTIFYOBJS) $(XYMONCOMMLIB)
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $(RPATHOPT) $(NOTIFYOBJS) $(XYMONCOMMLIBS) $(PCRELIBS)

acknowledgements.cgi: $(ACKNOWLEDGEOBJS) $(XYMONCOMMLIB)
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $(RPATHOPT) $(ACKNOWLEDGEOBJS) $(XYMONCOMMLIBS) $(PCRELIBS)

hostlist.cgi: $(HOSTLISTOBJS) $(XYMONCOMMLIB)
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $(RPATHOPT) $(HOSTLISTOBJS) $(XYMONCOMMLIBS) $(PCRELIBS)

perfdata.o: perfdata.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(PCREINCDIR) $(RRDDEF) $(RRDINCDIR) -c -o $@ $<

# Need -lm on perfdata because it refers to isnan()
perfdata.cgi: $(PERFDATAOBJS) $(XYMONCOMMLIB)
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $(RPATHOPT) $(PERFDATAOBJS) $(XYMONCOMMLIBS) $(PCRELIBS) $(RRDLIBS) -lm

useradm.cgi: $(USERADMOBJS) $(XYMONCOMMLIB)
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $(RPATHOPT) $(USERADMOBJS) $(XYMONCOMMLIBS) $(PCRELIBS)

chpasswd.cgi: $(CHPASSWDOBJS) $(XYMONCOMMLIB)
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $(RPATHOPT) $(CHPASSWDOBJS) $(XYMONCOMMLIBS) $(PCRELIBS)

appfeed.cgi: $(APPFEEDOBJS) $(XYMONLIB)
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $(RPATHOPT) $(APPFEEDOBJS) $(XYMONCOMMLIBS) $(XYMONLIBS) $(PCRELIBS)


%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<


clean:
	rm -f $(PROGRAMS) *.o *~

install: install-bin install-cgi install-man

# install-cgi hardlinks $(INSTALLBINDIR)/cgiwrap, which install-bin is what
# puts there. Listing both as prerequisites of "install" does not order them,
# so a parallel make can start install-cgi first and the very first ln fails
# on a missing source -- silently, because a shell "for" reports only its last
# iteration's status. Observed with make -j: history.sh, the first entry of
# CGISCRIPTS, absent from an otherwise complete install, giving a 404 on the
# history CGI.
install-cgi: install-bin

install-bin:
ifndef PKGBUILD
	chown -h $(XYMONUSER) $(PROGRAMS)
	chgrp -h `$(IDTOOL) -g $(XYMONUSER)` $(PROGRAMS)
	chmod 755 $(PROGRAMS)
endif
	# Remove each destination name first: $(INSTALLBINDIR) belongs to $(XYMONUSER),
	# so a name there may be a symlink cp -fp would follow and write through.
	for f in $(PROGRAMS); do rm -f $(INSTALLROOT)$(INSTALLBINDIR)/$$f; done
	cp -fp $(PROGRAMS) $(INSTALLROOT)$(INSTALLBINDIR)/

install-cgi:
	mkdir -p $(INSTALLROOT)$(CGIDIR)
	mkdir -p $(INSTALLROOT)$(SECURECGIDIR)
	# $(CGIDIR) belongs to $(XYMONUSER): a symlink left at a name would make
	# ln -f hard-link cgiwrap into the link's target directory. Remove the name
	# first so the link is made at the name.
	for F in $(CGISCRIPTS); do rm -f $(INSTALLROOT)$(CGIDIR)/$$F; ln -f $(INSTALLROOT)$(INSTALLBINDIR)/cgiwrap $(INSTALLROOT)$(CGIDIR)/$$F || exit 1; done
	for F in $(SECCGISCRIPTS); do rm -f $(INSTALLROOT)$(SECURECGIDIR)/$$F; ln -f $(INSTALLROOT)$(INSTALLBINDIR)/cgiwrap $(INSTALLROOT)$(SECURECGIDIR)/$$F || exit 1; done

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
	chmod 755 $(INSTALLROOT)$(MANROOT)/man1 $(INSTALLROOT)$(MANROOT)/man5 $(INSTALLROOT)$(MANROOT)/man8
endif
	for m in *.1; do rm -f $(INSTALLROOT)$(MANROOT)/man1/$$m; done
	cp -fp *.1 $(INSTALLROOT)$(MANROOT)/man1/
	for m in *.5; do rm -f $(INSTALLROOT)$(MANROOT)/man5/$$m; done
	cp -fp *.5 $(INSTALLROOT)$(MANROOT)/man5/
	for m in *.8; do rm -f $(INSTALLROOT)$(MANROOT)/man8/$$m; done
	cp -fp *.8 $(INSTALLROOT)$(MANROOT)/man8/

