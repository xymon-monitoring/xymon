# Makefile for xymongen
#

PROGRAMS = xymongen
GENOBJS = xymongen.o loadlayout.o loaddata.o pagegen.o process.o rssgen.o util.o debug.o csvreport.o

XYMONLIB = ../lib/libxymon.a
XYMONLIBS = $(XYMONLIB)
XYMONCOMMLIB = ../lib/libxymoncomm.a
XYMONCOMMLIBS = $(XYMONCOMMLIB) $(ZLIBLIBS) $(SSLLIBS) $(NETLIBS) $(LIBRTDEF)
XYMONTIMELIB = ../lib/libxymontime.a
XYMONTIMELIBS = $(XYMONTIMELIB) $(LIBRTDEF)

all: $(PROGRAMS)

xymongen: $(GENOBJS) $(XYMONTIMELIB) $(XYMONCOMMLIB)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(GENOBJS) $(XYMONTIMELIBS) $(XYMONCOMMLIBS) $(PCRELIBS)


################################################
# Default compilation rules
################################################
%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

clean:
	rm -f *.o *.a *~ $(PROGRAMS)


install: install-bin install-man

install-bin: $(PROGRAMS)
ifndef PKGBUILD
	chown -h $(XYMONUSER) $(PROGRAMS)
	chgrp -h `$(IDTOOL) -g $(XYMONUSER)` $(PROGRAMS)
	chmod 755 $(PROGRAMS) 
endif
	# Remove each destination name first: $(INSTALLBINDIR) belongs to $(XYMONUSER),
	# so a name there may be a symlink cp -fp would follow and write through.
	for f in $(PROGRAMS); do rm -f $(INSTALLROOT)$(INSTALLBINDIR)/$$f; done
	cp -fp $(PROGRAMS) $(INSTALLROOT)$(INSTALLBINDIR)/

install-man:
ifndef PKGBUILD
	chown -h $(XYMONUSER) *.1
	chgrp -h `$(IDTOOL) -g $(XYMONUSER)` *.1
	chmod 644 *.1
endif
	mkdir -p $(INSTALLROOT)$(MANROOT)/man1
ifndef PKGBUILD
	chown -h $(XYMONUSER) $(INSTALLROOT)$(MANROOT)/man1
	chgrp -h `$(IDTOOL) -g $(XYMONUSER)` $(INSTALLROOT)$(MANROOT)/man1
	chmod 755 $(INSTALLROOT)$(MANROOT)/man1
endif
	for m in *.1; do rm -f $(INSTALLROOT)$(MANROOT)/man1/$$m; done
	cp -fp *.1 $(INSTALLROOT)$(MANROOT)/man1/

