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


# install-safe places each file and directory without following a symlink at
# the destination name, owning and moding it as it goes. See client/Makefile.
INSTALLSAFE = ../build/install-safe
ifndef PKGBUILD
ISOWN = -o $(XYMONUSER) -g `$(IDTOOL) -g $(XYMONUSER)`
endif

install: install-bin install-man

install-bin: $(PROGRAMS)
	for f in $(PROGRAMS); do $(INSTALLSAFE) copy -m 755 $(ISOWN) $$f $(INSTALLROOT)$(INSTALLBINDIR)/$$f || exit 1; done

install-man:
	$(INSTALLSAFE) mkdir -m 755 $(ISOWN) $(INSTALLROOT)$(MANROOT)/man1
	for m in *.1; do $(INSTALLSAFE) copy -m 644 $(ISOWN) $$m $(INSTALLROOT)$(MANROOT)/man1/$$m || exit 1; done

