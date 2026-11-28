# xymonproxy Makefile

XYMONLIB = ../lib/libxymon.a
XYMONLIBS = $(XYMONLIB)
XYMONCOMMLIB = ../lib/libxymoncomm.a
XYMONCOMMLIBS = $(XYMONCOMMLIB) $(ZLIBLIBS) $(SSLLIBS) $(NETLIBS) $(LIBRTDEF)
XYMONTIMELIB = ../lib/libxymontime.a
XYMONTIMELIBS = $(XYMONTIMELIB) $(LIBRTDEF)

PROGRAMS = xymonproxy xymoncgimsg.cgi

PROXYOBJS = xymonproxy.o
MSGCGIOBJS = xymoncgimsg.o

all: $(PROGRAMS)

xymonproxy: $(PROXYOBJS) $(XYMONCOMMLIB) $(XYMONTIMELIB) $(XYMONLIB)
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $(PROXYOBJS) $(XYMONCOMMLIBS) $(XYMONTIMELIBS) $(XYMONLIBS)

xymoncgimsg.cgi: $(MSGCGIOBJS) $(XYMONCOMMLIB) $(XYMONLIB)
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $(MSGCGIOBJS) $(XYMONCOMMLIBS) $(XYMONLIBS) 

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

install-bin:
	for f in $(PROGRAMS); do $(INSTALLSAFE) copy -m 755 $(ISOWN) $$f $(INSTALLROOT)$(INSTALLBINDIR)/$$f || exit 1; done

install-man:
	$(INSTALLSAFE) mkdir -m 755 $(ISOWN) $(INSTALLROOT)$(MANROOT)/man8
	for m in *.8; do $(INSTALLSAFE) copy -m 644 $(ISOWN) $$m $(INSTALLROOT)$(MANROOT)/man8/$$m || exit 1; done

