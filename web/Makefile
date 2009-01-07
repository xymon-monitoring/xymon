PROGRAMS = bb-hist.cgi bb-eventlog.cgi bb-rep.cgi bb-replog.cgi bb-snapshot.cgi bb-findhost.cgi bb-csvinfo.cgi bb-ack.cgi bb-webpage bb-datepage.cgi hobbitgraph.cgi hobbitsvc.cgi hobbit-enadis.cgi hobbit-confreport.cgi hobbit-nkview.cgi hobbit-nkedit.cgi hobbit-ackinfo.cgi hobbit-statusreport.cgi boilerplate.cgi hobbit-hostgraphs.cgi hobbit-ghosts.cgi hobbit-notifylog.cgi hobbit-hostlist.cgi
CGISCRIPTS = bb-hist.sh bb-eventlog.sh bb-rep.sh bb-replog.sh bb-snapshot.sh bb-findhost.sh bb-csvinfo.sh hobbitcolumn.sh bb-datepage.sh hobbitgraph.sh bb-hostsvc.sh bb-histlog.sh hobbit-confreport.sh hobbit-confreport-critical.sh hobbit-nkview.sh hobbit-certreport.sh hobbit-nongreen.sh hobbit-hostgraphs.sh hobbit-ghosts.sh hobbit-notifylog.sh hobbit-hostlist.sh
SECCGISCRIPTS = bb-ack.sh hobbit-enadis.sh hobbit-nkedit.sh hobbit-ackinfo.sh

LIBOBJS = ../lib/libbbgen.a

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

HOSTGRAPHSOBJS  = hobbit-hostgraphs.o
BOILERPLATEOBJS = boilerplate.o


IDTOOL := $(shell if test `uname -s` = "SunOS"; then echo /usr/xpg4/bin/id; else echo id; fi)


all: $(PROGRAMS) $(CGISCRIPTS) $(SECCGISCRIPTS)

bb-hist.cgi: $(HISTOBJS) ../lib/libbbgen.a
	$(CC) $(CFLAGS) -o $@ $(HISTOBJS) ../lib/libbbgen.a $(PCRELIBS) $(NETLIBS)

bb-eventlog.cgi: $(EVENTLOGOBJS) ../lib/libbbgen.a
	$(CC) $(CFLAGS) -o $@ $(EVENTLOGOBJS) ../lib/libbbgen.a $(PCRELIBS) $(NETLIBS)

bb-rep.cgi: $(REPOBJS) ../lib/libbbgen.a
	$(CC) $(CFLAGS) -o $@ $(REPOBJS) ../lib/libbbgen.a $(PCRELIBS) $(NETLIBS)

bb-replog.cgi: $(REPLOGOBJS) ../lib/libbbgen.a
	$(CC) $(CFLAGS) -o $@ $(REPLOGOBJS) ../lib/libbbgen.a $(PCRELIBS) $(NETLIBS)

bb-snapshot.cgi: $(SNAPOBJS) ../lib/libbbgen.a
	$(CC) $(CFLAGS) -o $@ $(SNAPOBJS) ../lib/libbbgen.a $(PCRELIBS) $(NETLIBS)

bb-findhost.cgi: $(FINDHOSTOBJS) ../lib/libbbgen.a
	$(CC) $(CFLAGS) -o $@ $(FINDHOSTOBJS) ../lib/libbbgen.a $(PCRELIBS) $(NETLIBS)

bb-csvinfo.cgi: $(CSVINFOOBJS) ../lib/libbbgen.a
	$(CC) $(CFLAGS) -o $@ $(CSVINFOOBJS) ../lib/libbbgen.a $(PCRELIBS) $(NETLIBS)

bb-ack.cgi: $(ACKCGIOBJS) ../lib/libbbgen.a
	$(CC) $(CFLAGS) -o $@ $(ACKCGIOBJS) ../lib/libbbgen.a $(PCRELIBS) $(NETLIBS)

bb-webpage: $(WEBPAGEOBJS) ../lib/libbbgen.a
	$(CC) $(CFLAGS) -o $@ $(WEBPAGEOBJS) ../lib/libbbgen.a $(PCRELIBS) $(NETLIBS)

bb-datepage.cgi: $(DATEPAGEOBJS) ../lib/libbbgen.a
	$(CC) $(CFLAGS) -o $@ $(DATEPAGEOBJS) ../lib/libbbgen.a $(PCRELIBS) $(NETLIBS)

hobbitgraph.o: hobbitgraph.c
	$(CC) $(CFLAGS) $(PCREINCDIR) $(RRDDEF) $(RRDINCDIR) -c -o $@ $<

# Need NETLIBS on Solaris for getservbyname(), called by parse_url()
hobbitgraph.cgi: $(HOBBITGRAPHOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(RPATHOPT) $(HOBBITGRAPHOBJS) $(LIBOBJS) $(PCRELIBS) $(RRDLIBS) $(NETLIBS)

hobbitsvc.cgi: $(HOBBITSVCOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(HOBBITSVCOBJS) $(LIBOBJS) $(PCRELIBS) $(NETLIBS)

hobbit-enadis.cgi: $(HOBBITENADISOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(HOBBITENADISOBJS) $(LIBOBJS) $(PCRELIBS) $(NETLIBS)

hobbit-statusreport.cgi: $(STATUSREPOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(STATUSREPOBJS) $(LIBOBJS) $(NETLIBS)

hobbit-confreport.cgi: $(CONFREPOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(CONFREPOBJS) $(LIBOBJS) $(PCRELIBS) $(NETLIBS)

hobbit-nkview.cgi: $(NKVIEWOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(RPATHOPT) $(NKVIEWOBJS) $(LIBOBJS) $(PCRELIBS) $(NETLIBS)

hobbit-nkview.o: hobbit-nkview.c
	$(CC) $(CFLAGS) $(PCREINCDIR) -c -o $@ $<

hobbit-nkedit.cgi: $(NKEDITOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(RPATHOPT) $(NKEDITOBJS) $(LIBOBJS) $(PCRELIBS) $(NETLIBS)

hobbit-ackinfo.cgi: $(ACKINFOOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(RPATHOPT) $(ACKINFOOBJS) $(LIBOBJS) $(NETLIBS)

boilerplate.cgi: $(BOILERPLATEOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(RPATHOPT) $(BOILERPLATEOBJS) $(LIBOBJS) $(PCRELIBS) $(NETLIBS)

hobbit-hostgraphs.cgi: $(HOSTGRAPHSOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(RPATHOPT) $(HOSTGRAPHSOBJS) $(LIBOBJS) $(PCRELIBS) $(NETLIBS)

hobbit-ghosts.cgi: $(GHOSTOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(RPATHOPT) $(GHOSTOBJS) $(LIBOBJS) $(PCRELIBS) $(NETLIBS)

hobbit-notifylog.cgi: $(NOTIFYOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(RPATHOPT) $(NOTIFYOBJS) $(LIBOBJS) $(PCRELIBS) $(NETLIBS)

hobbit-hostlist.cgi: $(HOSTLISTOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(RPATHOPT) $(HOSTLISTOBJS) $(LIBOBJS) $(PCRELIBS) $(NETLIBS)


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


hobbit-hostlist.sh: hobbit-hostlist.sh.DIST
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

