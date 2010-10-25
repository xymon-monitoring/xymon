# Makefile for bbgen - bbdisplay programs
#

PROGRAMS = bbgen
GENOBJS = bbgen.o loadbbhosts.o loaddata.o pagegen.o process.o wmlgen.o rssgen.o util.o debug.o csvreport.o

all: $(PROGRAMS)

bbgen: $(GENOBJS) ../lib/libbbgen.a
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(GENOBJS) ../lib/libbbgen.a $(PCRELIBS) $(NETLIBS) $(LIBRTDEF)


################################################
# Default compilation rules
################################################
%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f *.o *.a *~ $(PROGRAMS)


install: install-bin install-man install-obsoleted

install-bin: $(PROGRAMS)
	cp -fp $(PROGRAMS) $(INSTALLROOT)$(INSTALLBINDIR)/

install-obsoleted:
	for f in bb-infocolumn bb-larrdcolumn bb-hostsvc.cgi; do if test -f $(INSTALLROOT)$(INSTALLBINDIR)/$f; then rm -f $(INSTALLROOT)$(INSTALLBINDIR)/$f; fi; done

install-man:
	mkdir -p $(INSTALLROOT)$(MANROOT)/man1 $(INSTALLROOT)$(MANROOT)/man5
	cp -fp *.1 $(INSTALLROOT)$(MANROOT)/man1/

