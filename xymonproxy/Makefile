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

install: install-bin install-man

install-bin:
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
	mkdir -p $(INSTALLROOT)$(MANROOT)/man8
ifndef PKGBUILD
	chown -h $(XYMONUSER) $(INSTALLROOT)$(MANROOT)/man8 *.8
	chgrp -h `$(IDTOOL) -g $(XYMONUSER)` $(INSTALLROOT)$(MANROOT)/man8 *.8
	chmod 755 $(INSTALLROOT)$(MANROOT)/man8
	chmod 644 *.8
endif
	for m in *.8; do rm -f $(INSTALLROOT)$(MANROOT)/man8/$$m; done
	cp -fp *.8 $(INSTALLROOT)$(MANROOT)/man8/

