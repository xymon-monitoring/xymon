# Makefile for bbgen - bbdisplay programs
#

PROGRAMS = bbgen bb-eventlog.cgi
CGISCRIPTS = bb-eventlog.sh
EXTENSIONS = bbcombotest

GENOBJS = bbgen.o loadbbhosts.o loaddata.o bbconvert.o pagegen.o eventlog.o acklog.o process.o wmlgen.o rssgen.o util.o debug.o csvreport.o
EVENTLOGOBJS = bb-eventlog.o util.o loadbbhosts.o		# util.o for histlogurl, find_or_create_column
COMBOTESTOBJS = bbcombotest.o


all: $(PROGRAMS) $(CGISCRIPTS) $(EXTENSIONS)


bbgen: $(GENOBJS) ../lib/libbbgen.a
	$(CC) $(CFLAGS) -o $@ $(GENOBJS) ../lib/libbbgen.a $(PCRELIBS) $(NETLIBS)

bb-eventlog.cgi: $(EVENTLOGOBJS) ../lib/libbbgen.a
	$(CC) $(CFLAGS) -o $@ $(EVENTLOGOBJS) ../lib/libbbgen.a $(PCRELIBS) $(NETLIBS)


bbcombotest: $(COMBOTESTOBJS) ../lib/libbbgen.a
	$(CC) $(CFLAGS) -o $@ $(COMBOTESTOBJS) ../lib/libbbgen.a $(PCRELIBS) $(NETLIBS)


################################################
# Objects requiring special compile flags 
################################################
digest.o: digest.c
	$(CC) $(CFLAGS) $(SSLFLAGS) $(SSLINCDIR) -c -o $@ digest.c

eventlog.o: eventlog.c
	$(CC) $(CFLAGS) $(PCREINCDIR) -c -o $@ eventlog.c


################################################
# Objects compiled differently for use in 
# bbgen/bbtest-net, and in standalone programs
################################################
bb-eventlog.o: eventlog.o
	$(CC) $(CFLAGS) -DCGI -c -o $@ $(PCREINCDIR) eventlog.c

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

