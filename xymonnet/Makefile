# Xymon - xymonnet Makefile

XYMONLIB = ../lib/libxymon.a
XYMONLIBS = $(XYMONLIB)
XYMONCOMMLIB = ../lib/libxymoncomm.a
XYMONCOMMLIBS = $(XYMONCOMMLIB) $(SSLLIBS) $(NETLIBS) $(LIBRTDEF)
XYMONTIMELIB = ../lib/libxymontime.a
XYMONTIMELIBS = $(XYMONTIMELIB) $(LIBRTDEF)

# ARES settings. c-ares is included
ifeq ($(SYSTEMCARES),no)
	CARESINC = -I./c-ares
	CARESLIBS = libcares.a
	LOCALCARES = libcares.a
endif

PROGRAMS = xymonnet xymonping beastat
EXTENSIONS = xymonnet-again.sh
SNMPPROGRAMS = xymon-snmpcollect
DBGTOOLS = contest


ifeq ($(DOSNMP),yes)
PROGRAMS += $(SNMPPROGRAMS)
endif
all: $(PROGRAMS) $(EXTENSIONS) $(DBGTOOLS)

NETTESTOBJS = xymonnet.o contest.o httptest.o httpresult.o ldaptest.o dns.o dns2.o httpcookies.o
PINGTESTOBJS = xymonping.o
BEASTATOBJS = beastat.o

xymonnet: $(NETTESTOBJS) $(XYMONCOMMLIB) $(XYMONTIMELIB) $(XYMONLIB)
	$(CC) $(CFLAGS) -o $@ $(RPATHOPT) $(NETTESTOBJS) $(CARESLIBS) $(LDAPLIBS) $(XYMONTIMELIBS) $(XYMONCOMMLIBS) $(XYMONLIBS) $(PCRELIBS)

xymonping: $(PINGTESTOBJS) $(XYMONTIMELIB) $(XYMONLIB)
	$(CC) $(CFLAGS) -o $@ $(PINGTESTOBJS) $(XYMONTIMELIBS) $(XYMONCOMMLIBS)

xymonnet.o: xymonnet.c
	$(CC) $(CFLAGS) $(SSLFLAGS) $(LDAPFLAGS) $(SSLINCDIR) $(LDAPINCDIR) -c -o $@ xymonnet.c

ldaptest.o: ldaptest.c
	$(CC) $(CFLAGS) $(LDAPFLAGS) $(LDAPINCDIR) -c -o $@ ldaptest.c

httptest.o: httptest.c
	$(CC) $(CFLAGS) $(SSLFLAGS) $(SSLINCDIR) -c -o $@ httptest.c

httpresult.o: httpresult.c
	$(CC) $(CFLAGS) $(SSLFLAGS) $(SSLINCDIR) -c -o $@ httpresult.c

contest.o: contest.c
	$(CC) $(CFLAGS) $(SSLFLAGS) $(SSLINCDIR) -c -o $@ contest.c

dns.o: $(LOCALCARES) dns.c
	$(CC) $(CFLAGS) $(CARESINC) -c -o $@ dns.c

dns2.o: dns2.c
	$(CC) $(CFLAGS) $(CARESINC) -c -o $@ dns2.c

libcares.a: c-ares/.libs/libcares.a
	ranlib c-ares/.libs/libcares.a || echo "ranlib failure - ignored"
	cp c-ares/.libs/libcares.a .

c-ares/.libs/libcares.a: c-ares/Makefile
	(cd c-ares && $(MAKE))

c-ares/Makefile: c-ares/configure
	(cd c-ares && ../c-ares-shim.sh ./configure --disable-shared)

c-ares/configure: c-ares-$(ARESVER).tar.gz
	gzip -dc $< | tar xf -
	mv c-ares-$(ARESVER) c-ares
	# Must touch "configure", or it will trigger a rebuild because it is older than the tar.gz file.
	touch c-ares/configure

beastat: $(BEASTATOBJS) $(XYMONCOMMLIB) $(XYMONTIMELIB)
	$(CC) $(CFLAGS) -o $@ $(RPATHOPT) $(BEASTATOBJS) $(XYMONCOMMLIBS) $(XYMONTIMELIBS) $(PCRELIBS)

beastat.o: beastat.c
	$(CC) $(CFLAGS) $(PCREINCDIR) -c -o $@ beastat.c


contest: contest.c httptest.o dns.o dns2.o httpcookies.o $(XYMONCOMMLIB) $(XYMONTIMELIB)
	$(CC) $(CFLAGS) -o contest -I../include $(CARESINC) -DSTANDALONE contest.c httptest.o dns.o dns2.o httpcookies.o $(CARESLIBS) $(XYMONCOMMLIBS) $(XYMONTIMELIBS)


xymon-snmpcollect: xymon-snmpcollect.o $(XYMONCOMMLIB) $(XYMONTIMELIB)
	$(CC) $(LDFLAGS) -o $@ xymon-snmpcollect.o `net-snmp-config --libs` $(XYMONLIBS) $(XYMONCOMMLIBS) $(XYMONTIMELIBS)

xymon-snmpcollect.o: xymon-snmpcollect.c
	$(CC) $(CFLAGS) -I. `net-snmp-config --cflags` -c -o $@ xymon-snmpcollect.c

################################################
# Default compilation rules
################################################
%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

%.sh: %.sh.DIST
	cat $< | sed -e 's!@XYMONHOME@!$(XYMONHOME)!g' | sed -e 's!@RUNTIMEDEFS@!$(RUNTIMEDEFS)!g' >$@
	chmod 755 $@

clean:
	rm -f *.o *.a *~ $(PROGRAMS) $(EXTENSIONS) $(DBGTOOLS)


install: install-bin install-ext install-config install-man

install-bin: $(PROGRAMS)
ifndef PKGBUILD
	chown $(XYMONUSER) $(PROGRAMS)
	chgrp `$(IDTOOL) -g $(XYMONUSER)` $(PROGRAMS)
	chmod 755 $(PROGRAMS)
endif
	cp -fp $(PROGRAMS) $(INSTALLROOT)$(INSTALLBINDIR)/

install-ext: $(EXTENSIONS)
ifndef PKGBUILD
	chown $(XYMONUSER) $(EXTENSIONS)
	chgrp `$(IDTOOL) -g $(XYMONUSER)` $(EXTENSIONS)
	chmod 755 $(EXTENSIONS)
endif
	cp -fp $(EXTENSIONS) $(INSTALLROOT)$(INSTALLEXTDIR)/

install-config:
	../build/convert-bbservices $(INSTALLROOT)$(INSTALLETCDIR)/protocols.cfg
	../build/merge-sects protocols.cfg $(INSTALLROOT)$(INSTALLETCDIR)/protocols.cfg
ifndef PKGBUILD
	chown $(XYMONUSER) $(INSTALLROOT)$(INSTALLETCDIR)/protocols.cfg
	chgrp `$(IDTOOL) -g $(XYMONUSER)` $(INSTALLROOT)$(INSTALLETCDIR)/protocols.cfg
	chmod 644 $(INSTALLROOT)$(INSTALLETCDIR)/protocols.cfg
endif

install-man:
ifndef PKGBUILD
	chown $(XYMONUSER) *.1 *.5
	chgrp `$(IDTOOL) -g $(XYMONUSER)` *.1 *.5
	chmod 644 *.1 *.5
endif
	mkdir -p $(INSTALLROOT)$(MANROOT)/man1 $(INSTALLROOT)$(MANROOT)/man5
ifndef PKGBUILD
	chown $(XYMONUSER) $(INSTALLROOT)$(MANROOT)/man1 $(INSTALLROOT)$(MANROOT)/man5
	chgrp `$(IDTOOL) -g $(XYMONUSER)` $(INSTALLROOT)$(MANROOT)/man1 $(INSTALLROOT)$(MANROOT)/man5
	chmod 755 $(INSTALLROOT)$(MANROOT)/man1 $(INSTALLROOT)$(MANROOT)/man5
endif
	cp -fp *.1 $(INSTALLROOT)$(MANROOT)/man1/
	cp -fp *.5 $(INSTALLROOT)$(MANROOT)/man5/

