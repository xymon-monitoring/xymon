PROGRAMS = hobbitgraph.cgi hobbitsvc.cgi hobbit-enadis.cgi hobbit-confreport.cgi hobbit-nkview.cgi hobbit-ackinfo.cgi hobbit-statusreport.cgi
CGISCRIPTS = hobbitgraph.sh bb-hostsvc.sh bb-histlog.sh hobbit-confreport.sh hobbit-nkview.sh hobbit-ackinfo.sh hobbit-certreport.sh
SECCGISCRIPTS = bb-ack.sh hobbit-enadis.sh

LIBOBJS = ../lib/libbbgen.a

HOBBITGRAPHOBJS = hobbitgraph.o
HOBBITSVCOBJS   = hobbitsvc.o hobbitsvc-info.o hobbitsvc-trends.o do_alert.o
HOBBITENADISOBJS= hobbit-enadis.o
NKVIEWOBJS      = hobbit-nkview.o
ACKINFOOBJS     = hobbit-ackinfo.o
CONFREPOBJS     = hobbit-confreport.o do_alert.o
STATUSREPOBJS   = hobbit-statusreport.o
MAILACKOBJS     = hobbit-mailack.o


IDTOOL := $(shell if test `uname -s` = "SunOS"; then echo /usr/xpg4/bin/id; else echo id; fi)


all: $(PROGRAMS) $(CGISCRIPTS) $(SECCGISCRIPTS)

# Need NETLIBS on Solaris for getservbyname(), called by parse_url()

do_alert.o: ../hobbitd/do_alert.o
	cp ../hobbitd/do_alert.o .

hobbitgraph.o: hobbitgraph.c
	$(CC) $(CFLAGS) $(PCREINCDIR) $(RRDDEF) $(RRDINCDIR) -c -o $@ $<

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
	$(CC) $(CFLAGS) $(PCREINCDIR) -c -o $@ hobbit-nkview.c

hobbit-ackinfo.cgi: $(ACKINFOOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(RPATHOPT) $(ACKINFOOBJS) $(LIBOBJS) $(NETLIBS)

bb-ack.sh: bb-ack.sh.DIST
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

hobbit-certreport.sh: hobbit-certreport.sh.DIST
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

install-man:
	mkdir -p $(INSTALLROOT)$(MANROOT)/man1 $(INSTALLROOT)$(MANROOT)/man5 $(INSTALLROOT)$(MANROOT)/man8
	cp -fp *.1 $(INSTALLROOT)$(MANROOT)/man1/
	cp -fp *.5 $(INSTALLROOT)$(MANROOT)/man5/
	cp -fp *.8 $(INSTALLROOT)$(MANROOT)/man8/

