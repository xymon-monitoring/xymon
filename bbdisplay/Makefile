# Makefile for bbgen - bbdisplay programs
#

PROGRAMS = bbgen bb-rep.cgi bb-replog.cgi bb-hist.cgi bb-snapshot.cgi bb-eventlog.cgi bb-findhost.cgi bb-csvinfo.cgi bb-ack.cgi bb-webpage bb-datepage.cgi

EXTENSIONS = bbcombotest

CGISCRIPTS = bb-rep.sh bb-replog.sh bb-hist.sh bb-snapshot.sh bb-eventlog.sh bb-findhost.sh bb-csvinfo.sh hobbitcolumn.sh bb-datepage.sh

GENOBJS = bbgen.o loadbbhosts.o loaddata.o reportdata.o bbconvert.o pagegen.o eventlog.o acklog.o process.o wmlgen.o rssgen.o bb-replog.o util.o debug.o csvreport.o
COMBOTESTOBJS = bbcombotest.o
REPOBJS = bb-rep.o
REPLOGOBJS = bb-replog-cgi.o reportdata.o
HISTOBJS = bb-hist.o reportdata.o
SNAPOBJS = bb-snapshot.o
EVENTLOGOBJS = bb-eventlog.o util.o loadbbhosts.o		# util.o for histlogurl, find_or_create_column
FINDHOSTOBJS = bb-findhost.o
CSVINFOOBJS = bb-csvinfo.o
ACKCGIOBJS = bb-ack.o
WEBPAGEOBJS = bb-webpage.o
DATEPAGEOBJS = bb-datepage.o


all: $(PROGRAMS) $(EXTENSIONS) $(CGISCRIPTS)


bbgen: $(GENOBJS) ../lib/libbbgen.a
	$(CC) $(CFLAGS) -o $@ $(GENOBJS) ../lib/libbbgen.a $(PCRELIBS) $(NETLIBS)

bbcombotest: $(COMBOTESTOBJS) ../lib/libbbgen.a
	$(CC) $(CFLAGS) -o $@ $(COMBOTESTOBJS) ../lib/libbbgen.a $(PCRELIBS) $(NETLIBS)

bb-rep.cgi: $(REPOBJS) ../lib/libbbgen.a
	$(CC) $(CFLAGS) -o $@ $(REPOBJS) ../lib/libbbgen.a $(PCRELIBS) $(NETLIBS)

bb-replog.cgi: $(REPLOGOBJS) ../lib/libbbgen.a
	$(CC) $(CFLAGS) -o $@ $(REPLOGOBJS) ../lib/libbbgen.a $(PCRELIBS) $(NETLIBS)

bb-hist.cgi: $(HISTOBJS) ../lib/libbbgen.a
	$(CC) $(CFLAGS) -o $@ $(HISTOBJS) ../lib/libbbgen.a $(PCRELIBS) $(NETLIBS)

bb-snapshot.cgi: $(SNAPOBJS) ../lib/libbbgen.a
	$(CC) $(CFLAGS) -o $@ $(SNAPOBJS) ../lib/libbbgen.a $(PCRELIBS) $(NETLIBS)

bb-eventlog.cgi: $(EVENTLOGOBJS) ../lib/libbbgen.a
	$(CC) $(CFLAGS) -o $@ $(EVENTLOGOBJS) ../lib/libbbgen.a $(PCRELIBS) $(NETLIBS)

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


################################################
# Objects requiring special compile flags 
################################################
digest.o: digest.c
	$(CC) $(CFLAGS) $(SSLFLAGS) $(SSLINCDIR) -c -o $@ digest.c

bb-hist.o: bb-hist.c
	$(CC) $(CFLAGS) $(HISTGRAPHDEF) -c -o $@ bb-hist.c


################################################
# Objects compiled differently for use in 
# bbgen/bbtest-net, and in standalone programs
################################################
bb-eventlog.o: eventlog.o
	$(CC) $(CFLAGS) -DCGI -c -o $@ eventlog.c

bb-replog-cgi.o: bb-replog.c
	$(CC) $(CFLAGS) -DCGI -c -o $@ bb-replog.c

################################################
# Default compilation rules
################################################
%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

%.sh: %.sh.DIST
	cat $< | sed -e 's!@BBHOME@!$(BBHOME)!g' | sed -e 's!@RUNTIMEDEFS@!$(RUNTIMEDEFS)!g' >$@
	chmod 755 $@

clean:
	rm -f *.o *.a *~ $(PROGRAMS) $(EXTENSIONS) $(CGISCRIPTS)


install: install-nocgi install-cgi

install-nocgi: install-bin install-exts install-man install-obsoleted

install-bin: $(PROGRAMS)
	cp -fp $(PROGRAMS) $(INSTALLROOT)$(INSTALLBINDIR)/

install-obsoleted:
	for f in bb-infocolumn bb-larrdcolumn bb-hostsvc.cgi; do if test -f $(INSTALLROOT)$(INSTALLBINDIR)/$f; then rm -f $(INSTALLROOT)$(INSTALLBINDIR)/$f; fi; done

install-exts: $(EXTENSIONS)
	cp -fp $(EXTENSIONS) $(INSTALLROOT)$(INSTALLEXTDIR)/

install-cgi: $(CGISCRIPTS)
	mkdir -p $(INSTALLROOT)$(CGIDIR)
	cp -fp $(CGISCRIPTS) $(INSTALLROOT)$(CGIDIR)/

install-man:
	mkdir -p $(INSTALLROOT)$(MANROOT)/man1 $(INSTALLROOT)$(MANROOT)/man5
	cp -fp *.1 $(INSTALLROOT)$(MANROOT)/man1/
	cp -fp *.5 $(INSTALLROOT)$(MANROOT)/man5/

