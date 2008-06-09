PROGRAMS = bb-hist.cgi bb-eventlog.cgi bb-rep.cgi bb-replog.cgi bb-snapshot.cgi bb-findhost.cgi bb-csvinfo.cgi bb-ack.cgi bb-webpage bb-datepage.cgi hobbitgraph.cgi hobbitsvc.cgi hobbit-enadis.cgi hobbit-confreport.cgi hobbit-nkview.cgi hobbit-nkedit.cgi hobbit-ackinfo.cgi hobbit-statusreport.cgi boilerplate.cgi hobbit-hostgraphs.cgi hobbit-ghosts.cgi hobbit-notifylog.cgi hobbit-hostlist.cgi hobbit-perfdata.cgi
CGISCRIPTS = bb-hist.sh bb-eventlog.sh bb-rep.sh bb-replog.sh bb-snapshot.sh bb-findhost.sh bb-csvinfo.sh hobbitcolumn.sh bb-datepage.sh hobbitgraph.sh bb-hostsvc.sh bb-histlog.sh hobbit-confreport.sh hobbit-confreport-critical.sh hobbit-nkview.sh hobbit-certreport.sh hobbit-nongreen.sh hobbit-hostgraphs.sh hobbit-ghosts.sh hobbit-notifylog.sh hobbit-topchanges.sh hobbit-hostlist.sh hobbit-perfdata.sh
SECCGISCRIPTS = bb-ack.sh hobbit-enadis.sh hobbit-nkedit.sh hobbit-ackinfo.sh

LIBOBJS = ../lib/$(HOBBITLIBRARY)

HISTOBJS        = bb-hist.o
EVENTLOGOBJS    = bb-eventlog.o
REPOBJS         = bb-rep.o
REPLOGOBJS      = bb-replog.o
SNAPOBJS        = bb-snapshot.o
FINDHOSTOBJS    = bb-findhost.o
CSVINFOOBJS     = bb-csvinfo.o
ACKCGIOBJS      = bb-ack.o
WEBPAGEOBJS     = bb-webpage.o
DATEPAGEOBJS    = bb-datepage.o

HOBBITGRAPHOBJS = hobbitgraph.o
HOBBITSVCOBJS   = hobbitsvc.o hobbitsvc-info.o hobbitsvc-trends.o
HOBBITENADISOBJS= hobbit-enadis.o
NKVIEWOBJS      = hobbit-nkview.o
NKEDITOBJS      = hobbit-nkedit.o
ACKINFOOBJS     = hobbit-ackinfo.o
CONFREPOBJS     = hobbit-confreport.o
STATUSREPOBJS   = hobbit-statusreport.o
MAILACKOBJS     = hobbit-mailack.o
GHOSTOBJS       = hobbit-ghosts.o
NOTIFYOBJS      = hobbit-notifylog.o
HOSTLISTOBJS    = hobbit-hostlist.o
PERFDATAOBJS    = hobbit-perfdata.o

HOSTGRAPHSOBJS  = hobbit-hostgraphs.o
BOILERPLATEOBJS = boilerplate.o


IDTOOL := $(shell if test `uname -s` = "SunOS"; then echo /usr/xpg4/bin/id; else echo id; fi)


all: $(PROGRAMS) $(CGISCRIPTS) $(SECCGISCRIPTS)

bb-hist.cgi: $(HISTOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(HISTOBJS) $(LIBOBJS) $(HOBBITZLIB) $(PCRELIBS) $(SSLLIBS) $(NETLIBS)

bb-eventlog.cgi: $(EVENTLOGOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(EVENTLOGOBJS) $(LIBOBJS) $(HOBBITZLIB) $(PCRELIBS) $(SSLLIBS) $(NETLIBS)

bb-rep.cgi: $(REPOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(REPOBJS) $(LIBOBJS) $(HOBBITZLIB) $(PCRELIBS) $(SSLLIBS) $(NETLIBS)

bb-replog.cgi: $(REPLOGOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(REPLOGOBJS) $(LIBOBJS) $(HOBBITZLIB) $(PCRELIBS) $(SSLLIBS) $(NETLIBS)

bb-snapshot.cgi: $(SNAPOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(SNAPOBJS) $(LIBOBJS) $(HOBBITZLIB) $(PCRELIBS) $(SSLLIBS) $(NETLIBS)

bb-findhost.cgi: $(FINDHOSTOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(FINDHOSTOBJS) $(LIBOBJS) $(HOBBITZLIB) $(PCRELIBS) $(SSLLIBS) $(NETLIBS)

bb-csvinfo.cgi: $(CSVINFOOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(CSVINFOOBJS) $(LIBOBJS) $(HOBBITZLIB) $(PCRELIBS) $(SSLLIBS) $(NETLIBS)

bb-ack.cgi: $(ACKCGIOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(ACKCGIOBJS) $(LIBOBJS) $(HOBBITZLIB) $(PCRELIBS) $(SSLLIBS) $(NETLIBS)

bb-webpage: $(WEBPAGEOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(WEBPAGEOBJS) $(LIBOBJS) $(HOBBITZLIB) $(PCRELIBS) $(SSLLIBS) $(NETLIBS)

bb-datepage.cgi: $(DATEPAGEOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(DATEPAGEOBJS) $(LIBOBJS) $(HOBBITZLIB) $(PCRELIBS) $(SSLLIBS) $(NETLIBS)

hobbitgraph.o: hobbitgraph.c
	$(CC) $(CFLAGS) $(PCREINCDIR) $(RRDDEF) $(RRDINCDIR) -c -o $@ $<

hobbit-perfdata.o: hobbit-perfdata.c
	$(CC) $(CFLAGS) $(PCREINCDIR) $(RRDDEF) $(RRDINCDIR) -c -o $@ $<

# Need NETLIBS on Solaris for getservbyname(), called by parse_url()
hobbitgraph.cgi: $(HOBBITGRAPHOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(RPATHOPT) $(HOBBITGRAPHOBJS) $(LIBOBJS) $(HOBBITZLIB) $(PCRELIBS) $(RRDLIBS) $(SSLLIBS) $(NETLIBS)

hobbitsvc.cgi: $(HOBBITSVCOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(HOBBITSVCOBJS) $(LIBOBJS) $(HOBBITZLIB) $(PCRELIBS) $(SSLLIBS) $(NETLIBS)

hobbit-enadis.cgi: $(HOBBITENADISOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(HOBBITENADISOBJS) $(LIBOBJS) $(HOBBITZLIB) $(PCRELIBS) $(SSLLIBS) $(NETLIBS)

hobbit-statusreport.cgi: $(STATUSREPOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(STATUSREPOBJS) $(LIBOBJS) $(HOBBITZLIB) $(SSLLIBS) $(NETLIBS)

hobbit-confreport.cgi: $(CONFREPOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(CONFREPOBJS) $(LIBOBJS) $(HOBBITZLIB) $(PCRELIBS) $(SSLLIBS) $(NETLIBS)

hobbit-nkview.cgi: $(NKVIEWOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(RPATHOPT) $(NKVIEWOBJS) $(LIBOBJS) $(HOBBITZLIB) $(PCRELIBS) $(SSLLIBS) $(NETLIBS)

hobbit-nkview.o: hobbit-nkview.c
	$(CC) $(CFLAGS) $(PCREINCDIR) -c -o $@ $<

hobbit-nkedit.cgi: $(NKEDITOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(RPATHOPT) $(NKEDITOBJS) $(LIBOBJS) $(HOBBITZLIB) $(PCRELIBS) $(SSLLIBS) $(NETLIBS)

hobbit-ackinfo.cgi: $(ACKINFOOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(RPATHOPT) $(ACKINFOOBJS) $(LIBOBJS) $(HOBBITZLIB) $(SSLLIBS) $(NETLIBS)

boilerplate.cgi: $(BOILERPLATEOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(RPATHOPT) $(BOILERPLATEOBJS) $(LIBOBJS) $(HOBBITZLIB) $(PCRELIBS) $(SSLLIBS) $(NETLIBS)

hobbit-hostgraphs.cgi: $(HOSTGRAPHSOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(RPATHOPT) $(HOSTGRAPHSOBJS) $(LIBOBJS) $(HOBBITZLIB) $(PCRELIBS) $(SSLLIBS) $(NETLIBS)

hobbit-ghosts.cgi: $(GHOSTOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(RPATHOPT) $(GHOSTOBJS) $(LIBOBJS) $(HOBBITZLIB) $(PCRELIBS) $(SSLLIBS) $(NETLIBS)

hobbit-notifylog.cgi: $(NOTIFYOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(RPATHOPT) $(NOTIFYOBJS) $(LIBOBJS) $(HOBBITZLIB) $(PCRELIBS) $(SSLLIBS) $(NETLIBS)

hobbit-hostlist.cgi: $(HOSTLISTOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(RPATHOPT) $(HOSTLISTOBJS) $(LIBOBJS) $(HOBBITZLIB) $(PCRELIBS) $(SSLLIBS) $(NETLIBS)

# Need -lm on hobbit-perfdata because it refers to isnan()
hobbit-perfdata.cgi: $(PERFDATAOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(RPATHOPT) $(PERFDATAOBJS) $(LIBOBJS) $(HOBBITZLIB) $(PCRELIBS) $(RRDLIBS) $(SSLLIBS) $(NETLIBS) -lm


bb-ack.sh: bb-ack.sh.DIST
	cat $< | sed -e 's!@BBHOME@!$(BBHOME)!g' | sed -e 's!@RUNTIMEDEFS@!$(RUNTIMEDEFS)!g' >$@
	chmod 755 $@

bb-csvinfo.sh: bb-csvinfo.sh.DIST
	cat $< | sed -e 's!@BBHOME@!$(BBHOME)!g' | sed -e 's!@RUNTIMEDEFS@!$(RUNTIMEDEFS)!g' >$@
	chmod 755 $@

bb-datepage.sh: bb-datepage.sh.DIST
	cat $< | sed -e 's!@BBHOME@!$(BBHOME)!g' | sed -e 's!@RUNTIMEDEFS@!$(RUNTIMEDEFS)!g' >$@
	chmod 755 $@

bb-eventlog.sh: bb-eventlog.sh.DIST
	cat $< | sed -e 's!@BBHOME@!$(BBHOME)!g' | sed -e 's!@RUNTIMEDEFS@!$(RUNTIMEDEFS)!g' >$@
	chmod 755 $@

bb-findhost.sh: bb-findhost.sh.DIST
	cat $< | sed -e 's!@BBHOME@!$(BBHOME)!g' | sed -e 's!@RUNTIMEDEFS@!$(RUNTIMEDEFS)!g' >$@
	chmod 755 $@

bb-hist.sh: bb-hist.sh.DIST
	cat $< | sed -e 's!@BBHOME@!$(BBHOME)!g' | sed -e 's!@RUNTIMEDEFS@!$(RUNTIMEDEFS)!g' >$@
	chmod 755 $@

bb-rep.sh: bb-rep.sh.DIST
	cat $< | sed -e 's!@BBHOME@!$(BBHOME)!g' | sed -e 's!@RUNTIMEDEFS@!$(RUNTIMEDEFS)!g' >$@
	chmod 755 $@

bb-replog.sh: bb-replog.sh.DIST
	cat $< | sed -e 's!@BBHOME@!$(BBHOME)!g' | sed -e 's!@RUNTIMEDEFS@!$(RUNTIMEDEFS)!g' >$@
	chmod 755 $@

bb-snapshot.sh: bb-snapshot.sh.DIST
	cat $< | sed -e 's!@BBHOME@!$(BBHOME)!g' | sed -e 's!@RUNTIMEDEFS@!$(RUNTIMEDEFS)!g' >$@
	chmod 755 $@

hobbitcolumn.sh: hobbitcolumn.sh.DIST
	cat $< | sed -e 's!@BBHOME@!$(BBHOME)!g' | sed -e 's!@RUNTIMEDEFS@!$(RUNTIMEDEFS)!g' >$@
	chmod 755 $@

hobbitgraph.sh: hobbitgraph.sh.DIST
	cat $< | sed -e 's!@BBHOME@!$(BBHOME)!g' | sed -e 's!@RUNTIMEDEFS@!$(RUNTIMEDEFS)!g' >$@
	chmod 755 $@

bb-hostsvc.sh: hobbitsvc.sh.DIST
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

bb-histlog.sh: hobbitsvc-hist.sh.DIST
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

hobbit-topchanges.sh: hobbit-topchanges.sh.DIST
	cat $< | sed -e 's!@BBHOME@!$(BBHOME)!g' | sed -e 's!@RUNTIMEDEFS@!$(RUNTIMEDEFS)!g' >$@
	chmod 755 $@

hobbit-hostlist.sh: hobbit-hostlist.sh.DIST
	cat $< | sed -e 's!@BBHOME@!$(BBHOME)!g' | sed -e 's!@RUNTIMEDEFS@!$(RUNTIMEDEFS)!g' >$@
	chmod 755 $@

hobbit-perfdata.sh: hobbit-perfdata.sh.DIST
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

