# Makefile for bbgen - bbdisplay programs
#

PROGRAMS = bbgen
EXTENSIONS = bbcombotest

GENOBJS = bbgen.o loadbbhosts.o loaddata.o bbconvert.o pagegen.o process.o wmlgen.o rssgen.o util.o debug.o csvreport.o
COMBOTESTOBJS = bbcombotest.o


all: $(PROGRAMS) $(EXTENSIONS)


bbgen: $(GENOBJS) ../lib/libbbgen.a
	$(CC) $(CFLAGS) -o $@ $(GENOBJS) ../lib/libbbgen.a $(PCRELIBS) $(NETLIBS)

bbcombotest: $(COMBOTESTOBJS) ../lib/libbbgen.a
	$(CC) $(CFLAGS) -o $@ $(COMBOTESTOBJS) ../lib/libbbgen.a $(PCRELIBS) $(NETLIBS)


################################################
# Default compilation rules
################################################
%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f *.o *.a *~ $(PROGRAMS) $(EXTENSIONS) $(CGISCRIPTS)


install: install-bin install-exts install-man install-obsoleted

install-bin: $(PROGRAMS)
	cp -fp $(PROGRAMS) $(INSTALLROOT)$(INSTALLBINDIR)/

install-obsoleted:
	for f in bb-infocolumn bb-larrdcolumn bb-hostsvc.cgi; do if test -f $(INSTALLROOT)$(INSTALLBINDIR)/$f; then rm -f $(INSTALLROOT)$(INSTALLBINDIR)/$f; fi; done

install-exts: $(EXTENSIONS)
	cp -fp $(EXTENSIONS) $(INSTALLROOT)$(INSTALLEXTDIR)/

install-man:
	mkdir -p $(INSTALLROOT)$(MANROOT)/man1 $(INSTALLROOT)$(MANROOT)/man5
	cp -fp *.1 $(INSTALLROOT)$(MANROOT)/man1/
	cp -fp *.5 $(INSTALLROOT)$(MANROOT)/man5/

