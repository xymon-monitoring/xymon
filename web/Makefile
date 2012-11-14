XYMONLIB = ../lib/libxymon.a
XYMONLIBS = $(XYMONLIB)
XYMONCOMMLIB = ../lib/libxymoncomm.a
XYMONCOMMLIBS = $(XYMONCOMMLIB) $(SSLLIBS) $(NETLIBS) $(LIBRTDEF)
XYMONTIMELIB = ../lib/libxymontime.a
XYMONTIMELIBS = $(XYMONTIMELIB) $(LIBRTDEF)

PROGRAMS = history.cgi eventlog.cgi report.cgi reportlog.cgi snapshot.cgi findhost.cgi csvinfo.cgi acknowledge.cgi xymonpage datepage.cgi svcstatus.cgi enadis.cgi confreport.cgi criticalview.cgi criticaleditor.cgi ackinfo.cgi statusreport.cgi boilerplate.cgi hostgraphs.cgi ghostlist.cgi notifications.cgi hostlist.cgi useradm.cgi appfeed.cgi
CGISCRIPTS = history.sh eventlog.sh report.sh reportlog.sh snapshot.sh findhost.sh csvinfo.sh columndoc.sh datepage.sh svcstatus.sh historylog.sh confreport.sh confreport-critical.sh criticalview.sh certreport.sh nongreen.sh hostgraphs.sh ghostlist.sh notifications.sh hostlist.sh topchanges.sh appfeed.sh appfeed-critical.sh
SECCGISCRIPTS = acknowledge.sh enadis.sh criticaleditor.sh ackinfo.sh useradm.sh

ifeq ($(DORRD),yes)
	PROGRAMS += showgraph.cgi perfdata.cgi
	CGISCRIPTS += showgraph.sh perfdata.sh
endif

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
HOSTLISTOBJS    = hostlist.o
PERFDATAOBJS	= perfdata.o
USERADMOBJS	= useradm.o

HOSTGRAPHSOBJS  = hostgraphs.o
BOILERPLATEOBJS = boilerplate.o


IDTOOL := $(shell if test `uname -s` = "SunOS"; then echo /usr/xpg4/bin/id; else echo id; fi)


all: $(PROGRAMS) $(CGISCRIPTS) $(SECCGISCRIPTS)

history.cgi: $(HISTOBJS) $(XYMONCOMMLIB)
	$(CC) $(CFLAGS) -o $@ $(HISTOBJS) $(XYMONCOMMLIBS) $(PCRELIBS)

eventlog.cgi: $(EVENTLOGOBJS) $(XYMONCOMMLIB)
	$(CC) $(CFLAGS) -o $@ $(EVENTLOGOBJS) $(XYMONCOMMLIBS) $(PCRELIBS)

report.cgi: $(REPOBJS) $(XYMONCOMMLIB)
	$(CC) $(CFLAGS) -o $@ $(REPOBJS) $(XYMONCOMMLIBS) $(PCRELIBS)

reportlog.cgi: $(REPLOGOBJS) $(XYMONCOMMLIB)
	$(CC) $(CFLAGS) -o $@ $(REPLOGOBJS) $(XYMONCOMMLIBS) $(PCRELIBS)

snapshot.cgi: $(SNAPOBJS) $(XYMONCOMMLIB)
	$(CC) $(CFLAGS) -o $@ $(SNAPOBJS) $(XYMONCOMMLIBS) $(PCRELIBS)

findhost.cgi: $(FINDHOSTOBJS) $(XYMONCOMMLIB)
	$(CC) $(CFLAGS) -o $@ $(FINDHOSTOBJS) $(XYMONCOMMLIBS) $(PCRELIBS)

csvinfo.cgi: $(CSVINFOOBJS) $(XYMONCOMMLIB)
	$(CC) $(CFLAGS) -o $@ $(CSVINFOOBJS) $(XYMONCOMMLIBS) $(PCRELIBS)

acknowledge.cgi: $(ACKCGIOBJS) $(XYMONCOMMLIB)
	$(CC) $(CFLAGS) -o $@ $(ACKCGIOBJS) $(XYMONCOMMLIBS) $(PCRELIBS)

xymonpage: $(WEBPAGEOBJS) $(XYMONCOMMLIB)
	$(CC) $(CFLAGS) -o $@ $(WEBPAGEOBJS) $(XYMONCOMMLIBS) $(PCRELIBS)

datepage.cgi: $(DATEPAGEOBJS) $(XYMONCOMMLIB)
	$(CC) $(CFLAGS) -o $@ $(DATEPAGEOBJS) $(XYMONCOMMLIBS) $(PCRELIBS)

showgraph.o: showgraph.c
	$(CC) $(CFLAGS) $(PCREINCDIR) $(RRDDEF) $(RRDINCDIR) -c -o $@ $<

# Need NETLIBS on Solaris for getservbyname(), called by parse_url()
showgraph.cgi: $(SHOWGRAPHOBJS) $(XYMONCOMMLIB)
	$(CC) $(CFLAGS) -o $@ $(RPATHOPT) $(SHOWGRAPHOBJS) $(XYMONCOMMLIBS) $(PCRELIBS) $(RRDLIBS)

svcstatus.cgi: $(SVCSTATUSOBJS) $(XYMONCOMMLIB)
	$(CC) $(CFLAGS) -o $@ $(SVCSTATUSOBJS) $(XYMONCOMMLIBS) $(PCRELIBS)

enadis.cgi: $(ENADISOBJS) $(XYMONCOMMLIB)
	$(CC) $(CFLAGS) -o $@ $(ENADISOBJS) $(XYMONCOMMLIBS) $(PCRELIBS)

statusreport.cgi: $(STATUSREPOBJS) $(XYMONCOMMLIB)
	$(CC) $(CFLAGS) -o $@ $(STATUSREPOBJS) $(XYMONCOMMLIBS) $(PCRELIBS)

confreport.cgi: $(CONFREPOBJS) $(XYMONCOMMLIB)
	$(CC) $(CFLAGS) -o $@ $(CONFREPOBJS) $(XYMONCOMMLIBS) $(PCRELIBS)

criticalview.cgi: $(CRITVIEWOBJS) $(XYMONCOMMLIB)
	$(CC) $(CFLAGS) -o $@ $(RPATHOPT) $(CRITVIEWOBJS) $(XYMONCOMMLIBS) $(PCRELIBS)

criticalview.o: criticalview.c
	$(CC) $(CFLAGS) $(PCREINCDIR) -c -o $@ $<

criticaleditor.cgi: $(CRITEDITOBJS) $(XYMONCOMMLIB)
	$(CC) $(CFLAGS) -o $@ $(RPATHOPT) $(CRITEDITOBJS) $(XYMONCOMMLIBS) $(PCRELIBS)

ackinfo.cgi: $(ACKINFOOBJS) $(XYMONCOMMLIB)
	$(CC) $(CFLAGS) -o $@ $(RPATHOPT) $(ACKINFOOBJS) $(XYMONCOMMLIBS)

boilerplate.cgi: $(BOILERPLATEOBJS) $(XYMONCOMMLIB)
	$(CC) $(CFLAGS) -o $@ $(RPATHOPT) $(BOILERPLATEOBJS) $(XYMONCOMMLIBS) $(PCRELIBS)

hostgraphs.cgi: $(HOSTGRAPHSOBJS) $(XYMONCOMMLIB)
	$(CC) $(CFLAGS) -o $@ $(RPATHOPT) $(HOSTGRAPHSOBJS) $(XYMONCOMMLIBS) $(PCRELIBS)

ghostlist.cgi: $(GHOSTOBJS) $(XYMONCOMMLIB)
	$(CC) $(CFLAGS) -o $@ $(RPATHOPT) $(GHOSTOBJS) $(XYMONCOMMLIBS) $(PCRELIBS)

notifications.cgi: $(NOTIFYOBJS) $(XYMONCOMMLIB)
	$(CC) $(CFLAGS) -o $@ $(RPATHOPT) $(NOTIFYOBJS) $(XYMONCOMMLIBS) $(PCRELIBS)

hostlist.cgi: $(HOSTLISTOBJS) $(XYMONCOMMLIB)
	$(CC) $(CFLAGS) -o $@ $(RPATHOPT) $(HOSTLISTOBJS) $(XYMONCOMMLIBS) $(PCRELIBS)

perfdata.o: perfdata.c
	$(CC) $(CFLAGS) $(PCREINCDIR) $(RRDDEF) $(RRDINCDIR) -c -o $@ $<
	#
# Need -lm on perfdata because it refers to isnan()
perfdata.cgi: $(PERFDATAOBJS) $(XYMONCOMMLIB)
	$(CC) $(CFLAGS) -o $@ $(RPATHOPT) $(PERFDATAOBJS) $(XYMONCOMMLIBS) $(PCRELIBS) $(RRDLIBS) -lm

useradm.cgi: $(USERADMOBJS) $(XYMONCOMMLIB)
	$(CC) $(CFLAGS) -o $@ $(RPATHOPT) $(USERADMOBJS) $(XYMONCOMMLIBS) $(PCRELIBS)

appfeed.cgi: $(APPFEEDOBJS) $(XYMONLIB)
	$(CC) $(CFLAGS) -o $@ $(RPATHOPT) $(APPFEEDOBJS) $(XYMONCOMMLIBS) $(XYMONLIBS) $(PCRELIBS)


acknowledge.sh: acknowledge.sh.DIST
	cat $< | sed -e 's!@XYMONHOME@!$(XYMONHOME)!g' | sed -e 's!@RUNTIMEDEFS@!$(RUNTIMEDEFS)!g' >$@
	chmod 755 $@

csvinfo.sh: csvinfo.sh.DIST
	cat $< | sed -e 's!@XYMONHOME@!$(XYMONHOME)!g' | sed -e 's!@RUNTIMEDEFS@!$(RUNTIMEDEFS)!g' >$@
	chmod 755 $@

datepage.sh: datepage.sh.DIST
	cat $< | sed -e 's!@XYMONHOME@!$(XYMONHOME)!g' | sed -e 's!@RUNTIMEDEFS@!$(RUNTIMEDEFS)!g' >$@
	chmod 755 $@

eventlog.sh: eventlog.sh.DIST
	cat $< | sed -e 's!@XYMONHOME@!$(XYMONHOME)!g' | sed -e 's!@RUNTIMEDEFS@!$(RUNTIMEDEFS)!g' >$@
	chmod 755 $@

findhost.sh: findhost.sh.DIST
	cat $< | sed -e 's!@XYMONHOME@!$(XYMONHOME)!g' | sed -e 's!@RUNTIMEDEFS@!$(RUNTIMEDEFS)!g' >$@
	chmod 755 $@

history.sh: history.sh.DIST
	cat $< | sed -e 's!@XYMONHOME@!$(XYMONHOME)!g' | sed -e 's!@RUNTIMEDEFS@!$(RUNTIMEDEFS)!g' >$@
	chmod 755 $@

report.sh: report.sh.DIST
	cat $< | sed -e 's!@XYMONHOME@!$(XYMONHOME)!g' | sed -e 's!@RUNTIMEDEFS@!$(RUNTIMEDEFS)!g' >$@
	chmod 755 $@

reportlog.sh: reportlog.sh.DIST
	cat $< | sed -e 's!@XYMONHOME@!$(XYMONHOME)!g' | sed -e 's!@RUNTIMEDEFS@!$(RUNTIMEDEFS)!g' >$@
	chmod 755 $@

snapshot.sh: snapshot.sh.DIST
	cat $< | sed -e 's!@XYMONHOME@!$(XYMONHOME)!g' | sed -e 's!@RUNTIMEDEFS@!$(RUNTIMEDEFS)!g' >$@
	chmod 755 $@

columndoc.sh: columndoc.sh.DIST
	cat $< | sed -e 's!@XYMONHOME@!$(XYMONHOME)!g' | sed -e 's!@RUNTIMEDEFS@!$(RUNTIMEDEFS)!g' >$@
	chmod 755 $@

showgraph.sh: showgraph.sh.DIST
	cat $< | sed -e 's!@XYMONHOME@!$(XYMONHOME)!g' | sed -e 's!@RUNTIMEDEFS@!$(RUNTIMEDEFS)!g' >$@
	chmod 755 $@

svcstatus.sh: svcstatus.sh.DIST
	cat $< | sed -e 's!@XYMONHOME@!$(XYMONHOME)!g' | sed -e 's!@RUNTIMEDEFS@!$(RUNTIMEDEFS)!g' >$@
	chmod 755 $@

criticalview.sh: criticalview.sh.DIST
	cat $< | sed -e 's!@XYMONHOME@!$(XYMONHOME)!g' | sed -e 's!@RUNTIMEDEFS@!$(RUNTIMEDEFS)!g' >$@
	chmod 755 $@

criticaleditor.sh: criticaleditor.sh.DIST
	cat $< | sed -e 's!@XYMONHOME@!$(XYMONHOME)!g' | sed -e 's!@RUNTIMEDEFS@!$(RUNTIMEDEFS)!g' >$@
	chmod 755 $@

ackinfo.sh: ackinfo.sh.DIST
	cat $< | sed -e 's!@XYMONHOME@!$(XYMONHOME)!g' | sed -e 's!@RUNTIMEDEFS@!$(RUNTIMEDEFS)!g' >$@
	chmod 755 $@

enadis.sh: enadis.sh.DIST
	cat $< | sed -e 's!@XYMONHOME@!$(XYMONHOME)!g' | sed -e 's!@RUNTIMEDEFS@!$(RUNTIMEDEFS)!g' >$@
	chmod 755 $@

historylog.sh: svcstatus-hist.sh.DIST
	cat $< | sed -e 's!@XYMONHOME@!$(XYMONHOME)!g' | sed -e 's!@RUNTIMEDEFS@!$(RUNTIMEDEFS)!g' >$@
	chmod 755 $@

confreport.sh: confreport.sh.DIST
	cat $< | sed -e 's!@XYMONHOME@!$(XYMONHOME)!g' | sed -e 's!@RUNTIMEDEFS@!$(RUNTIMEDEFS)!g' >$@
	chmod 755 $@

confreport-critical.sh: confreport-critical.sh.DIST
	cat $< | sed -e 's!@XYMONHOME@!$(XYMONHOME)!g' | sed -e 's!@RUNTIMEDEFS@!$(RUNTIMEDEFS)!g' >$@
	chmod 755 $@

certreport.sh: certreport.sh.DIST
	cat $< | sed -e 's!@XYMONHOME@!$(XYMONHOME)!g' | sed -e 's!@RUNTIMEDEFS@!$(RUNTIMEDEFS)!g' >$@
	chmod 755 $@

nongreen.sh: nongreen.sh.DIST
	cat $< | sed -e 's!@XYMONHOME@!$(XYMONHOME)!g' | sed -e 's!@RUNTIMEDEFS@!$(RUNTIMEDEFS)!g' >$@
	chmod 755 $@

hostgraphs.sh: hostgraphs.sh.DIST
	cat $< | sed -e 's!@XYMONHOME@!$(XYMONHOME)!g' | sed -e 's!@RUNTIMEDEFS@!$(RUNTIMEDEFS)!g' >$@
	chmod 755 $@

ghostlist.sh: ghostlist.sh.DIST
	cat $< | sed -e 's!@XYMONHOME@!$(XYMONHOME)!g' | sed -e 's!@RUNTIMEDEFS@!$(RUNTIMEDEFS)!g' >$@
	chmod 755 $@

notifications.sh: notifications.sh.DIST
	cat $< | sed -e 's!@XYMONHOME@!$(XYMONHOME)!g' | sed -e 's!@RUNTIMEDEFS@!$(RUNTIMEDEFS)!g' >$@
	chmod 755 $@


hostlist.sh: hostlist.sh.DIST
	cat $< | sed -e 's!@XYMONHOME@!$(XYMONHOME)!g' | sed -e 's!@RUNTIMEDEFS@!$(RUNTIMEDEFS)!g' >$@
	chmod 755 $@

perfdata.sh: perfdata.sh.DIST
	cat $< | sed -e 's!@XYMONHOME@!$(XYMONHOME)!g' | sed -e 's!@RUNTIMEDEFS@!$(RUNTIMEDEFS)!g' >$@
	chmod 755 $@

useradm.sh: useradm.sh.DIST
	cat $< | sed -e 's!@XYMONHOME@!$(XYMONHOME)!g' | sed -e 's!@RUNTIMEDEFS@!$(RUNTIMEDEFS)!g' >$@
	chmod 755 $@

appfeed.sh: appfeed.sh.DIST
	cat $< | sed -e 's!@XYMONHOME@!$(XYMONHOME)!g' | sed -e 's!@RUNTIMEDEFS@!$(RUNTIMEDEFS)!g' >$@
	chmod 755 $@

appfeed-critical.sh: appfeed-critical.sh.DIST
	cat $< | sed -e 's!@XYMONHOME@!$(XYMONHOME)!g' | sed -e 's!@RUNTIMEDEFS@!$(RUNTIMEDEFS)!g' >$@
	chmod 755 $@

topchanges.sh: topchanges.sh.DIST
	cat $< | sed -e 's!@XYMONHOME@!$(XYMONHOME)!g' | sed -e 's!@RUNTIMEDEFS@!$(RUNTIMEDEFS)!g' >$@
	chmod 755 $@


%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<


clean:
	rm -f $(PROGRAMS) $(CGISCRIPTS) $(SECCGISCRIPTS) *.o *~

install: install-bin install-cgi install-man

install-bin:
ifndef PKGBUILD
	chown $(XYMONUSER) $(PROGRAMS)
	chgrp `$(IDTOOL) -g $(XYMONUSER)` $(PROGRAMS)
	chmod 755 $(PROGRAMS)
endif
	cp -fp $(PROGRAMS) $(INSTALLROOT)$(INSTALLBINDIR)/

install-cgi:
ifndef PKGBUILD
	chown $(XYMONUSER) $(CGISCRIPTS) $(SECCGISCRIPTS)
	chgrp `$(IDTOOL) -g $(XYMONUSER)` $(CGISCRIPTS) $(SECCGISCRIPTS)
	chmod 755 $(CGISCRIPTS) $(SECCGISCRIPTS)
endif
	mkdir -p $(INSTALLROOT)$(CGIDIR)
	cp -fp $(CGISCRIPTS) $(INSTALLROOT)$(CGIDIR)/
	mkdir -p $(INSTALLROOT)$(SECURECGIDIR)
	cp -fp $(SECCGISCRIPTS) $(INSTALLROOT)$(SECURECGIDIR)/
ifndef PKGBUILD
	chown $(XYMONUSER) $(INSTALLROOT)$(CGIDIR) $(INSTALLROOT)$(SECURECGIDIR)
	chgrp `$(IDTOOL) -g $(XYMONUSER)` $(INSTALLROOT)$(CGIDIR) $(INSTALLROOT)$(SECURECGIDIR)
	chmod 755 $(INSTALLROOT)$(CGIDIR) $(INSTALLROOT)$(SECURECGIDIR)
endif

install-man:
ifndef PKGBUILD
	chown $(XYMONUSER) *.1 *.5 *.8
	chgrp `$(IDTOOL) -g $(XYMONUSER)` *.1 *.5 *.8
	chmod 644 *.1 *.5 *.8
endif
	mkdir -p $(INSTALLROOT)$(MANROOT)/man1 $(INSTALLROOT)$(MANROOT)/man5 $(INSTALLROOT)$(MANROOT)/man8
ifndef PKGBUILD
	chown $(XYMONUSER) $(INSTALLROOT)$(MANROOT)/man1 $(INSTALLROOT)$(MANROOT)/man5 $(INSTALLROOT)$(MANROOT)/man8
	chgrp `$(IDTOOL) -g $(XYMONUSER)` $(INSTALLROOT)$(MANROOT)/man1 $(INSTALLROOT)$(MANROOT)/man5 $(INSTALLROOT)$(MANROOT)/man8
	chmod 755 $(INSTALLROOT)$(MANROOT)/man1 $(INSTALLROOT)$(MANROOT)/man5 $(INSTALLROOT)$(MANROOT)/man8
endif
	cp -fp *.1 $(INSTALLROOT)$(MANROOT)/man1/
	cp -fp *.5 $(INSTALLROOT)$(MANROOT)/man5/
	cp -fp *.8 $(INSTALLROOT)$(MANROOT)/man8/

