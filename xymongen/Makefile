# Makefile for xymongen
#

PROGRAMS = xymongen
GENOBJS = xymongen.o loadlayout.o loaddata.o pagegen.o process.o wmlgen.o rssgen.o util.o debug.o csvreport.o

XYMONLIB = ../lib/libxymon.a
XYMONLIBS = -lxymon $(PCRELIBS)
XYMONCOMMLIB = ../lib/libxymoncomm.a
XYMONCOMMLIBS = -lxymoncomm $(COMPLIBS) $(SSLLIBS) $(NETLIBS) $(LIBRTDEF) $(PCRELIBS)
XYMONTIMELIB = ../lib/libxymontime.a
XYMONTIMELIBS = -lxymontime $(LIBRTDEF)

all: $(PROGRAMS)

xymongen: $(GENOBJS) $(XYMONTIMELIB) $(XYMONCOMMLIB)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(GENOBJS) $(XYMONTIMELIBS) $(XYMONCOMMLIBS) $(PCRELIBS)


################################################
# Default compilation rules
################################################
%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f *.o *.a *~ $(PROGRAMS)


install: install-bin install-man

install-bin: $(PROGRAMS)
ifndef PKGBUILD
	chown $(XYMONUSER) $(PROGRAMS)
	chgrp `$(IDTOOL) -g $(XYMONUSER)` $(PROGRAMS)
	chmod 755 $(PROGRAMS) 
endif
	cp -fp $(PROGRAMS) $(INSTALLROOT)$(INSTALLBINDIR)/

install-man:
ifndef PKGBUILD
	chown $(XYMONUSER) *.1
	chgrp `$(IDTOOL) -g $(XYMONUSER)` *.1
	chmod 644 *.1
endif
	mkdir -p $(INSTALLROOT)$(MANROOT)/man1
ifndef PKGBUILD
	chown $(XYMONUSER) $(INSTALLROOT)$(MANROOT)/man1
	chgrp `$(IDTOOL) -g $(XYMONUSER)` $(INSTALLROOT)$(MANROOT)/man1
	chmod 755 $(INSTALLROOT)$(MANROOT)/man1
endif
	cp -fp *.1 $(INSTALLROOT)$(MANROOT)/man1/

