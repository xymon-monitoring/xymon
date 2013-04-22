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
	$(CC) $(CFLAGS) -o $@ $(PROXYOBJS) $(XYMONCOMMLIBS) $(XYMONTIMELIBS) $(XYMONLIBS)

xymoncgimsg.cgi: $(MSGCGIOBJS) $(XYMONCOMMLIB) $(XYMONLIB)
	$(CC) $(CFLAGS) -o $@ $(MSGCGIOBJS) $(XYMONCOMMLIBS) $(XYMONLIBS) 

################################################
# Default compilation rules
################################################
%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f *.o *.a *~ $(PROGRAMS)

install: install-bin install-man

install-bin:
ifndef PKGBUILD
	chown $(XYMONUSER) $(PROGRAMS)
	chgrp `$(IDTOOL) -g $(XYMONUSER)` $(PROGRAMS)
	chmod 755 $(PROGRAMS)
endif
	cp -fp $(PROGRAMS) $(INSTALLROOT)$(INSTALLBINDIR)/

install-man:
	mkdir -p $(INSTALLROOT)$(MANROOT)/man8
ifndef PKGBUILD
	chown $(XYMONUSER) $(INSTALLROOT)$(MANROOT)/man8 *.8
	chgrp `$(IDTOOL) -g $(XYMONUSER)` $(INSTALLROOT)$(MANROOT)/man8 *.8
	chmod 755 $(INSTALLROOT)$(MANROOT)/man8
	chmod 644 *.8
endif
	cp -fp *.8 $(INSTALLROOT)$(MANROOT)/man8/

