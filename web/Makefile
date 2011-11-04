PROGRAMS = history.cgi eventlog.cgi report.cgi reportlog.cgi snapshot.cgi findhost.cgi csvinfo.cgi acknowledge.cgi xymonpage datepage.cgi showgraph.cgi svcstatus.cgi enadis.cgi confreport.cgi criticalview.cgi criticaleditor.cgi ackinfo.cgi statusreport.cgi boilerplate.cgi hostgraphs.cgi ghostlist.cgi notifications.cgi hostlist.cgi perfdata.cgi useradm.cgi appfeed.cgi
CGISCRIPTS = history.sh eventlog.sh report.sh reportlog.sh snapshot.sh findhost.sh csvinfo.sh columndoc.sh datepage.sh showgraph.sh svcstatus.sh historylog.sh confreport.sh confreport-critical.sh criticalview.sh certreport.sh nongreen.sh hostgraphs.sh ghostlist.sh notifications.sh hostlist.sh perfdata.sh topchanges.sh appfeed.sh
SECCGISCRIPTS = acknowledge.sh enadis.sh criticaleditor.sh ackinfo.sh useradm.sh

LIBOBJS = ../lib/libxymon.a

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

history.cgi: $(HISTOBJS) ../lib/libxymon.a
	$(CC) $(CFLAGS) -o $@ $(HISTOBJS) ../lib/libxymon.a $(PCRELIBS) $(NETLIBS) $(LIBRTDEF)

eventlog.cgi: $(EVENTLOGOBJS) ../lib/libxymon.a
	$(CC) $(CFLAGS) -o $@ $(EVENTLOGOBJS) ../lib/libxymon.a $(PCRELIBS) $(NETLIBS) $(LIBRTDEF)

report.cgi: $(REPOBJS) ../lib/libxymon.a
	$(CC) $(CFLAGS) -o $@ $(REPOBJS) ../lib/libxymon.a $(PCRELIBS) $(NETLIBS) $(LIBRTDEF)

reportlog.cgi: $(REPLOGOBJS) ../lib/libxymon.a
	$(CC) $(CFLAGS) -o $@ $(REPLOGOBJS) ../lib/libxymon.a $(PCRELIBS) $(NETLIBS) $(LIBRTDEF)

snapshot.cgi: $(SNAPOBJS) ../lib/libxymon.a
	$(CC) $(CFLAGS) -o $@ $(SNAPOBJS) ../lib/libxymon.a $(PCRELIBS) $(NETLIBS) $(LIBRTDEF)

findhost.cgi: $(FINDHOSTOBJS) ../lib/libxymon.a
	$(CC) $(CFLAGS) -o $@ $(FINDHOSTOBJS) ../lib/libxymon.a $(PCRELIBS) $(NETLIBS) $(LIBRTDEF)

csvinfo.cgi: $(CSVINFOOBJS) ../lib/libxymon.a
	$(CC) $(CFLAGS) -o $@ $(CSVINFOOBJS) ../lib/libxymon.a $(PCRELIBS) $(NETLIBS) $(LIBRTDEF)

acknowledge.cgi: $(ACKCGIOBJS) ../lib/libxymon.a
	$(CC) $(CFLAGS) -o $@ $(ACKCGIOBJS) ../lib/libxymon.a $(PCRELIBS) $(NETLIBS) $(LIBRTDEF)

xymonpage: $(WEBPAGEOBJS) ../lib/libxymon.a
	$(CC) $(CFLAGS) -o $@ $(WEBPAGEOBJS) ../lib/libxymon.a $(PCRELIBS) $(NETLIBS) $(LIBRTDEF)

datepage.cgi: $(DATEPAGEOBJS) ../lib/libxymon.a
	$(CC) $(CFLAGS) -o $@ $(DATEPAGEOBJS) ../lib/libxymon.a $(PCRELIBS) $(NETLIBS) $(LIBRTDEF)

showgraph.o: showgraph.c
	$(CC) $(CFLAGS) $(PCREINCDIR) $(RRDDEF) $(RRDINCDIR) -c -o $@ $<

# Need NETLIBS on Solaris for getservbyname(), called by parse_url()
showgraph.cgi: $(SHOWGRAPHOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(RPATHOPT) $(SHOWGRAPHOBJS) $(LIBOBJS) $(PCRELIBS) $(RRDLIBS) $(NETLIBS) $(LIBRTDEF)

svcstatus.cgi: $(SVCSTATUSOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(SVCSTATUSOBJS) $(LIBOBJS) $(PCRELIBS) $(NETLIBS) $(LIBRTDEF)

enadis.cgi: $(ENADISOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(ENADISOBJS) $(LIBOBJS) $(PCRELIBS) $(NETLIBS) $(LIBRTDEF)

statusreport.cgi: $(STATUSREPOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(STATUSREPOBJS) $(LIBOBJS) $(PCRELIBS) $(NETLIBS) $(LIBRTDEF)

confreport.cgi: $(CONFREPOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(CONFREPOBJS) $(LIBOBJS) $(PCRELIBS) $(NETLIBS) $(LIBRTDEF)

criticalview.cgi: $(CRITVIEWOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(RPATHOPT) $(CRITVIEWOBJS) $(LIBOBJS) $(PCRELIBS) $(NETLIBS) $(LIBRTDEF)

criticalview.o: criticalview.c
	$(CC) $(CFLAGS) $(PCREINCDIR) -c -o $@ $<

criticaleditor.cgi: $(CRITEDITOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(RPATHOPT) $(CRITEDITOBJS) $(LIBOBJS) $(PCRELIBS) $(NETLIBS) $(LIBRTDEF)

ackinfo.cgi: $(ACKINFOOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(RPATHOPT) $(ACKINFOOBJS) $(LIBOBJS) $(NETLIBS) $(LIBRTDEF)

boilerplate.cgi: $(BOILERPLATEOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(RPATHOPT) $(BOILERPLATEOBJS) $(LIBOBJS) $(PCRELIBS) $(NETLIBS) $(LIBRTDEF)

hostgraphs.cgi: $(HOSTGRAPHSOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(RPATHOPT) $(HOSTGRAPHSOBJS) $(LIBOBJS) $(PCRELIBS) $(NETLIBS) $(LIBRTDEF)

ghostlist.cgi: $(GHOSTOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(RPATHOPT) $(GHOSTOBJS) $(LIBOBJS) $(PCRELIBS) $(NETLIBS) $(LIBRTDEF)

notifications.cgi: $(NOTIFYOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(RPATHOPT) $(NOTIFYOBJS) $(LIBOBJS) $(PCRELIBS) $(NETLIBS) $(LIBRTDEF)

hostlist.cgi: $(HOSTLISTOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(RPATHOPT) $(HOSTLISTOBJS) $(LIBOBJS) $(PCRELIBS) $(NETLIBS) $(LIBRTDEF)

perfdata.o: perfdata.c
	$(CC) $(CFLAGS) $(PCREINCDIR) $(RRDDEF) $(RRDINCDIR) -c -o $@ $<
	#
# Need -lm on perfdata because it refers to isnan()
perfdata.cgi: $(PERFDATAOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(RPATHOPT) $(PERFDATAOBJS) $(LIBOBJS) $(PCRELIBS) $(RRDLIBS) $(NETLIBS) -lm $(LIBRTDEF)

useradm.cgi: $(USERADMOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(RPATHOPT) $(USERADMOBJS) $(LIBOBJS) $(PCRELIBS) $(NETLIBS) $(LIBRTDEF)

appfeed.cgi: $(APPFEEDOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(RPATHOPT) $(APPFEEDOBJS) $(LIBOBJS) $(PCRELIBS) $(NETLIBS) $(LIBRTDEF)


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

topchanges.sh: topchanges.sh.DIST
	cat $< | sed -e 's!@XYMONHOME@!$(XYMONHOME)!g' | sed -e 's!@RUNTIMEDEFS@!$(RUNTIMEDEFS)!g' >$@
	chmod 755 $@


%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<


clean:
	rm -f $(PROGRAMS) $(CGISCRIPTS) $(SECCGISCRIPTS) *.o *~

install: install-bin install-cgi install-man

install-bin:
	cp -fp $(PROGRAMS) $(INSTALLROOT)$(INSTALLBINDIR)/

install-cgi:
	mkdir -p $(INSTALLROOT)$(CGIDIR)
	cp -fp $(CGISCRIPTS) $(INSTALLROOT)$(CGIDIR)/
	mkdir -p $(INSTALLROOT)$(SECURECGIDIR)
	cp -fp $(SECCGISCRIPTS) $(INSTALLROOT)$(SECURECGIDIR)/
ifndef PKGBUILD
	chown $(XYMONUSER) $(INSTALLROOT)$(CGIDIR) $(INSTALLROOT)$(CGIDIR)/* $(INSTALLROOT)$(SECURECGIDIR) $(INSTALLROOT)$(SECURECGIDIR)/*
	chgrp `$(IDTOOL) -g $(XYMONUSER)` $(INSTALLROOT)$(CGIDIR) $(INSTALLROOT)$(CGIDIR)/* $(INSTALLROOT)$(SECURECGIDIR) $(INSTALLROOT)$(SECURECGIDIR)/*
endif

install-man:
	mkdir -p $(INSTALLROOT)$(MANROOT)/man1 $(INSTALLROOT)$(MANROOT)/man5 $(INSTALLROOT)$(MANROOT)/man8
	cp -fp *.1 $(INSTALLROOT)$(MANROOT)/man1/
	cp -fp *.5 $(INSTALLROOT)$(MANROOT)/man5/
	cp -fp *.8 $(INSTALLROOT)$(MANROOT)/man8/

