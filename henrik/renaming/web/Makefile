PROGRAMS = history.cgi eventlog.cgi report.cgi reportlog.cgi snapshot.cgi findhost.cgi csvinfo.cgi acknowledge.cgi xymonpage datepage.cgi hobbitgraph.cgi svcstatus.cgi hobbit-enadis.cgi hobbit-confreport.cgi hobbit-nkview.cgi hobbit-nkedit.cgi hobbit-ackinfo.cgi hobbit-statusreport.cgi boilerplate.cgi hobbit-hostgraphs.cgi hobbit-ghosts.cgi hobbit-notifylog.cgi hobbit-hostlist.cgi hobbit-perfdata.cgi hobbit-useradm.cgi
CGISCRIPTS = history.sh eventlog.sh report.sh reportlog.sh snapshot.sh findhost.sh csvinfo.sh hobbitcolumn.sh datepage.sh hobbitgraph.sh svcstatus.sh historylog.sh hobbit-confreport.sh hobbit-confreport-critical.sh hobbit-nkview.sh hobbit-certreport.sh hobbit-nongreen.sh hobbit-hostgraphs.sh hobbit-ghosts.sh hobbit-notifylog.sh hobbit-hostlist.sh hobbit-perfdata.sh hobbit-topchanges.sh
SECCGISCRIPTS = acknowledge.sh hobbit-enadis.sh hobbit-nkedit.sh hobbit-ackinfo.sh hobbit-useradm.sh

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

HOBBITGRAPHOBJS = hobbitgraph.o
SVCSTATUSOBJS   = svcstatus.o svcstatus-info.o svcstatus-trends.o
HOBBITENADISOBJS= hobbit-enadis.o
NKVIEWOBJS      = hobbit-nkview.o
NKEDITOBJS      = hobbit-nkedit.o
ACKINFOOBJS     = hobbit-ackinfo.o
CONFREPOBJS     = hobbit-confreport.o
STATUSREPOBJS   = hobbit-statusreport.o
MAILACKOBJS     = xymon-mailack.o
GHOSTOBJS       = hobbit-ghosts.o
NOTIFYOBJS      = hobbit-notifylog.o
HOSTLISTOBJS    = hobbit-hostlist.o
PERFDATAOBJS	= hobbit-perfdata.o
USERADMOBJS	= hobbit-useradm.o

HOSTGRAPHSOBJS  = hobbit-hostgraphs.o
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

hobbitgraph.o: hobbitgraph.c
	$(CC) $(CFLAGS) $(PCREINCDIR) $(RRDDEF) $(RRDINCDIR) -c -o $@ $<

# Need NETLIBS on Solaris for getservbyname(), called by parse_url()
hobbitgraph.cgi: $(HOBBITGRAPHOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(RPATHOPT) $(HOBBITGRAPHOBJS) $(LIBOBJS) $(PCRELIBS) $(RRDLIBS) $(NETLIBS) $(LIBRTDEF)

svcstatus.cgi: $(SVCSTATUSOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(SVCSTATUSOBJS) $(LIBOBJS) $(PCRELIBS) $(NETLIBS) $(LIBRTDEF)

hobbit-enadis.cgi: $(HOBBITENADISOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(HOBBITENADISOBJS) $(LIBOBJS) $(PCRELIBS) $(NETLIBS) $(LIBRTDEF)

hobbit-statusreport.cgi: $(STATUSREPOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(STATUSREPOBJS) $(LIBOBJS) $(NETLIBS) $(LIBRTDEF)

hobbit-confreport.cgi: $(CONFREPOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(CONFREPOBJS) $(LIBOBJS) $(PCRELIBS) $(NETLIBS) $(LIBRTDEF)

hobbit-nkview.cgi: $(NKVIEWOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(RPATHOPT) $(NKVIEWOBJS) $(LIBOBJS) $(PCRELIBS) $(NETLIBS) $(LIBRTDEF)

hobbit-nkview.o: hobbit-nkview.c
	$(CC) $(CFLAGS) $(PCREINCDIR) -c -o $@ $<

hobbit-nkedit.cgi: $(NKEDITOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(RPATHOPT) $(NKEDITOBJS) $(LIBOBJS) $(PCRELIBS) $(NETLIBS) $(LIBRTDEF)

hobbit-ackinfo.cgi: $(ACKINFOOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(RPATHOPT) $(ACKINFOOBJS) $(LIBOBJS) $(NETLIBS) $(LIBRTDEF)

boilerplate.cgi: $(BOILERPLATEOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(RPATHOPT) $(BOILERPLATEOBJS) $(LIBOBJS) $(PCRELIBS) $(NETLIBS) $(LIBRTDEF)

hobbit-hostgraphs.cgi: $(HOSTGRAPHSOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(RPATHOPT) $(HOSTGRAPHSOBJS) $(LIBOBJS) $(PCRELIBS) $(NETLIBS) $(LIBRTDEF)

hobbit-ghosts.cgi: $(GHOSTOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(RPATHOPT) $(GHOSTOBJS) $(LIBOBJS) $(PCRELIBS) $(NETLIBS) $(LIBRTDEF)

hobbit-notifylog.cgi: $(NOTIFYOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(RPATHOPT) $(NOTIFYOBJS) $(LIBOBJS) $(PCRELIBS) $(NETLIBS) $(LIBRTDEF)

hobbit-hostlist.cgi: $(HOSTLISTOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(RPATHOPT) $(HOSTLISTOBJS) $(LIBOBJS) $(PCRELIBS) $(NETLIBS) $(LIBRTDEF)

hobbit-perfdata.o: hobbit-perfdata.c
	$(CC) $(CFLAGS) $(PCREINCDIR) $(RRDDEF) $(RRDINCDIR) -c -o $@ $<
	#
# Need -lm on hobbit-perfdata because it refers to isnan()
hobbit-perfdata.cgi: $(PERFDATAOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(RPATHOPT) $(PERFDATAOBJS) $(LIBOBJS) $(PCRELIBS) $(RRDLIBS) $(NETLIBS) -lm $(LIBRTDEF)

hobbit-useradm.cgi: $(USERADMOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(RPATHOPT) $(USERADMOBJS) $(LIBOBJS) $(PCRELIBS) $(NETLIBS) $(LIBRTDEF)


acknowledge.sh: acknowledge.sh.DIST
	cat $< | sed -e 's!@BBHOME@!$(BBHOME)!g' | sed -e 's!@RUNTIMEDEFS@!$(RUNTIMEDEFS)!g' >$@
	chmod 755 $@

csvinfo.sh: csvinfo.sh.DIST
	cat $< | sed -e 's!@BBHOME@!$(BBHOME)!g' | sed -e 's!@RUNTIMEDEFS@!$(RUNTIMEDEFS)!g' >$@
	chmod 755 $@

datepage.sh: datepage.sh.DIST
	cat $< | sed -e 's!@BBHOME@!$(BBHOME)!g' | sed -e 's!@RUNTIMEDEFS@!$(RUNTIMEDEFS)!g' >$@
	chmod 755 $@

eventlog.sh: eventlog.sh.DIST
	cat $< | sed -e 's!@BBHOME@!$(BBHOME)!g' | sed -e 's!@RUNTIMEDEFS@!$(RUNTIMEDEFS)!g' >$@
	chmod 755 $@

findhost.sh: findhost.sh.DIST
	cat $< | sed -e 's!@BBHOME@!$(BBHOME)!g' | sed -e 's!@RUNTIMEDEFS@!$(RUNTIMEDEFS)!g' >$@
	chmod 755 $@

history.sh: history.sh.DIST
	cat $< | sed -e 's!@BBHOME@!$(BBHOME)!g' | sed -e 's!@RUNTIMEDEFS@!$(RUNTIMEDEFS)!g' >$@
	chmod 755 $@

report.sh: report.sh.DIST
	cat $< | sed -e 's!@BBHOME@!$(BBHOME)!g' | sed -e 's!@RUNTIMEDEFS@!$(RUNTIMEDEFS)!g' >$@
	chmod 755 $@

reportlog.sh: reportlog.sh.DIST
	cat $< | sed -e 's!@BBHOME@!$(BBHOME)!g' | sed -e 's!@RUNTIMEDEFS@!$(RUNTIMEDEFS)!g' >$@
	chmod 755 $@

snapshot.sh: snapshot.sh.DIST
	cat $< | sed -e 's!@BBHOME@!$(BBHOME)!g' | sed -e 's!@RUNTIMEDEFS@!$(RUNTIMEDEFS)!g' >$@
	chmod 755 $@

hobbitcolumn.sh: hobbitcolumn.sh.DIST
	cat $< | sed -e 's!@BBHOME@!$(BBHOME)!g' | sed -e 's!@RUNTIMEDEFS@!$(RUNTIMEDEFS)!g' >$@
	chmod 755 $@

hobbitgraph.sh: hobbitgraph.sh.DIST
	cat $< | sed -e 's!@BBHOME@!$(BBHOME)!g' | sed -e 's!@RUNTIMEDEFS@!$(RUNTIMEDEFS)!g' >$@
	chmod 755 $@

svcstatus.sh: svcstatus.sh.DIST
	cat $< | sed -e 's!@BBHOME@!$(BBHOME)!g' | sed -e 's!@RUNTIMEDEFS@!$(RUNTIMEDEFS)!g' >$@
	chmod 755 $@

hobbit-nkview.sh: hobbit-nkview.sh.DIST
	cat $< | sed -e 's!@BBHOME@!$(BBHOME)!g' | sed -e 's!@RUNTIMEDEFS@!$(RUNTIMEDEFS)!g' >$@
	chmod 755 $@

hobbit-nkedit.sh: hobbit-nkedit.sh.DIST
	cat $< | sed -e 's!@BBHOME@!$(BBHOME)!g' | sed -e 's!@RUNTIMEDEFS@!$(RUNTIMEDEFS)!g' >$@
	chmod 755 $@

hobbit-ackinfo.sh: hobbit-ackinfo.sh.DIST
	cat $< | sed -e 's!@BBHOME@!$(BBHOME)!g' | sed -e 's!@RUNTIMEDEFS@!$(RUNTIMEDEFS)!g' >$@
	chmod 755 $@

hobbit-enadis.sh: hobbit-enadis.sh.DIST
	cat $< | sed -e 's!@BBHOME@!$(BBHOME)!g' | sed -e 's!@RUNTIMEDEFS@!$(RUNTIMEDEFS)!g' >$@
	chmod 755 $@

historylog.sh: svcstatus-hist.sh.DIST
	cat $< | sed -e 's!@BBHOME@!$(BBHOME)!g' | sed -e 's!@RUNTIMEDEFS@!$(RUNTIMEDEFS)!g' >$@
	chmod 755 $@

hobbit-confreport.sh: hobbit-confreport.sh.DIST
	cat $< | sed -e 's!@BBHOME@!$(BBHOME)!g' | sed -e 's!@RUNTIMEDEFS@!$(RUNTIMEDEFS)!g' >$@
	chmod 755 $@

hobbit-confreport-critical.sh: hobbit-confreport-critical.sh.DIST
	cat $< | sed -e 's!@BBHOME@!$(BBHOME)!g' | sed -e 's!@RUNTIMEDEFS@!$(RUNTIMEDEFS)!g' >$@
	chmod 755 $@

hobbit-certreport.sh: hobbit-certreport.sh.DIST
	cat $< | sed -e 's!@BBHOME@!$(BBHOME)!g' | sed -e 's!@RUNTIMEDEFS@!$(RUNTIMEDEFS)!g' >$@
	chmod 755 $@

hobbit-nongreen.sh: hobbit-nongreen.sh.DIST
	cat $< | sed -e 's!@BBHOME@!$(BBHOME)!g' | sed -e 's!@RUNTIMEDEFS@!$(RUNTIMEDEFS)!g' >$@
	chmod 755 $@

hobbit-hostgraphs.sh: hobbit-hostgraphs.sh.DIST
	cat $< | sed -e 's!@BBHOME@!$(BBHOME)!g' | sed -e 's!@RUNTIMEDEFS@!$(RUNTIMEDEFS)!g' >$@
	chmod 755 $@

hobbit-ghosts.sh: hobbit-ghosts.sh.DIST
	cat $< | sed -e 's!@BBHOME@!$(BBHOME)!g' | sed -e 's!@RUNTIMEDEFS@!$(RUNTIMEDEFS)!g' >$@
	chmod 755 $@

hobbit-notifylog.sh: hobbit-notifylog.sh.DIST
	cat $< | sed -e 's!@BBHOME@!$(BBHOME)!g' | sed -e 's!@RUNTIMEDEFS@!$(RUNTIMEDEFS)!g' >$@
	chmod 755 $@


hobbit-hostlist.sh: hobbit-hostlist.sh.DIST
	cat $< | sed -e 's!@BBHOME@!$(BBHOME)!g' | sed -e 's!@RUNTIMEDEFS@!$(RUNTIMEDEFS)!g' >$@
	chmod 755 $@

hobbit-perfdata.sh: hobbit-perfdata.sh.DIST
	cat $< | sed -e 's!@BBHOME@!$(BBHOME)!g' | sed -e 's!@RUNTIMEDEFS@!$(RUNTIMEDEFS)!g' >$@
	chmod 755 $@

hobbit-useradm.sh: hobbit-useradm.sh.DIST
	cat $< | sed -e 's!@BBHOME@!$(BBHOME)!g' | sed -e 's!@RUNTIMEDEFS@!$(RUNTIMEDEFS)!g' >$@
	chmod 755 $@

hobbit-topchanges.sh: hobbit-topchanges.sh.DIST
	cat $< | sed -e 's!@BBHOME@!$(BBHOME)!g' | sed -e 's!@RUNTIMEDEFS@!$(RUNTIMEDEFS)!g' >$@
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
	chown $(BBUSER) $(INSTALLROOT)$(CGIDIR) $(INSTALLROOT)$(CGIDIR)/* $(INSTALLROOT)$(SECURECGIDIR) $(INSTALLROOT)$(SECURECGIDIR)/*
	chgrp `$(IDTOOL) -g $(BBUSER)` $(INSTALLROOT)$(CGIDIR) $(INSTALLROOT)$(CGIDIR)/* $(INSTALLROOT)$(SECURECGIDIR) $(INSTALLROOT)$(SECURECGIDIR)/*
endif

install-man:
	mkdir -p $(INSTALLROOT)$(MANROOT)/man1 $(INSTALLROOT)$(MANROOT)/man5 $(INSTALLROOT)$(MANROOT)/man8
	cp -fp *.1 $(INSTALLROOT)$(MANROOT)/man1/
	cp -fp *.5 $(INSTALLROOT)$(MANROOT)/man5/
	cp -fp *.8 $(INSTALLROOT)$(MANROOT)/man8/

