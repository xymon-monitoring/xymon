# Xymon - xymonnet Makefile

XYMONLIB = ../lib/libxymon.a
XYMONLIBS = $(XYMONLIB)
XYMONCOMMLIB = ../lib/libxymoncomm.a
XYMONCOMMLIBS = $(XYMONCOMMLIB) $(ZLIBLIBS) $(SSLLIBS) $(NETLIBS) $(LIBRTDEF)
XYMONTIMELIB = ../lib/libxymontime.a
XYMONTIMELIBS = $(XYMONTIMELIB) $(LIBRTDEF)

# ARES settings. c-ares is included
DNSFLAGS = -I./c-ares

PROGRAMS = xymonnet2 netmodule fping4 fping6
SNMPPROGRAMS = xymon-snmpcollect

ifeq ($(DOLDAP),yes)
PROGRAMS += ldaptalk
endif
ifeq ($(DOSNMP),yes)
PROGRAMS += $(SNMPPROGRAMS)
endif

all: $(PROGRAMS) fping.8

NETTESTOBJS = xymonnet2.o setuptests.o netdialog.o tcptalk.o ntptalk.o dnstalk.o httpcookies.o dnsbits.o sendresults.o netsql.o
NETMODULEOBJS = netmodule.o sendresults.o netsql.o
LDAPTALKOBJS = ldaptalk.o sendresults.o netsql.o

xymonnet2: libcares.a $(NETTESTOBJS) $(XYMONCOMMLIB) $(XYMONTIMELIB) $(XYMONLIB)
	$(CC) $(CFLAGS) -o $@ $(RPATHOPT) $(NETTESTOBJS) libcares.a $(XYMONTIMELIBS) $(XYMONCOMMLIBS) $(PCRELIBS) $(SQLITELIBS)

netmodule: $(NETMODULEOBJS) $(XYMONCOMMLIB) $(XYMONCOMMLIB)
	$(CC) $(CFLAGS) -o $@ $(RPATHOPT) $(NETMODULEOBJS) $(XYMONTIMELIB) $(XYMONCOMMLIBS) $(SQLITELIBS)

ldaptalk: $(LDAPTALKOBJS) $(XYMONCOMMLIB) $(XYMONCOMMLIB)
	$(CC) $(CFLAGS) -o $@ $(RPATHOPT) $(LDAPTALKOBJS) $(XYMONTIMELIB) $(XYMONCOMMLIBS) $(LDAPLIBS) $(SQLITELIBS)

ldaptalk.o: ldaptalk.c
	$(CC) $(CFLAGS) -c -o $@ $(LDAPFLAGS) $(LDAPINCDIR) ldaptalk.c

tcptalk.o: tcptalk.c tcptalk.h dnstalk.h ntptalk.h
	$(CC) $(CFLAGS) -c -o $@ tcptalk.c

dnstalk.o: dnstalk.c dnstalk.h
	$(CC) $(CFLAGS) $(DNSFLAGS) -c -o $@ dnstalk.c

dnsbits.o: dnsbits.c dnsbits.h
	$(CC) $(CFLAGS) $(DNSFLAGS) -c -o $@ dnsbits.c

sendresults.o: sendresults.c sendresults.h
	$(CC) $(CFLAGS) $(LDAPFLAGS) -c -o $@ sendresults.c


libcares.a: c-ares/.libs/libcares.a
	ranlib c-ares/.libs/libcares.a || echo "ranlib failure - ignored"
	cp c-ares/.libs/libcares.a .

c-ares/.libs/libcares.a: c-ares/Makefile
	(cd c-ares && $(MAKE))

c-ares/Makefile: c-ares/configure
	(cd c-ares && CFLAGS="$(CFLAGS)" ./configure --disable-shared)

c-ares/configure: c-ares-$(ARESVER).tar.gz
	gzip -dc $< | tar xf -
	mv c-ares-$(ARESVER) c-ares
	# Must touch "configure", or it will trigger a rebuild because it is older than the tar.gz file.
	touch c-ares/configure


fping.8: fping4
	cp fping.ipv4/fping.8 .

fping4: fping.ipv4/Makefile
	(cd fping.ipv4 && $(MAKE))
	cp fping.ipv4/fping ./fping4

fping6: fping.ipv6/Makefile
	(cd fping.ipv6 && $(MAKE))
	cp fping.ipv6/fping ./fping6

fping.ipv4/Makefile: fping-$(FPINGVER).tar.gz
	gzip -dc $< | tar xf -
	mv fping-$(FPINGVER) fping.ipv4
	# Must touch "configure", or it will trigger a rebuild because it is older than the tar.gz file.
	touch fping.ipv4/configure
	(cd fping.ipv4 && ./configure)

fping.ipv6/Makefile: fping-$(FPINGVER).tar.gz
	gzip -dc $< | tar xf -
	mv fping-$(FPINGVER) fping.ipv6
	# Must touch "configure", or it will trigger a rebuild because it is older than the tar.gz file.
	touch fping.ipv6/configure
	(cd fping.ipv6 && ./configure && echo "#define IPV6 1" >> config.h)


xymon-snmpcollect: xymon-snmpcollect.o $(LIBOBJS)
	$(CC) $(LDFLAGS) -o $@ xymon-snmpcollect.o `net-snmp-config --libs` $(XYMONCOMMLIBS) $(XYMONTIMELIBS) $(SSLLIBS)

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
	rm -f *.o *.a *~ fping.8 $(PROGRAMS) 

install: install-bin install-config install-man

install-bin: $(PROGRAMS)
ifndef PKGBUILD
	chown $(XYMONUSER) $(PROGRAMS)
	chgrp `$(IDTOOL) -g $(XYMONUSER)` $(PROGRAMS)
	chmod 755 $(PROGRAMS)
endif
	cp -fp $(PROGRAMS) $(INSTALLROOT)$(INSTALLBINDIR)/

install-config:
	../build/convert-bbservices $(INSTALLROOT)$(INSTALLETCDIR)/protocols2.cfg
	../build/merge-sects protocols2.cfg $(INSTALLROOT)$(INSTALLETCDIR)/protocols2.cfg
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

	#cp -fp *.1 $(INSTALLROOT)$(MANROOT)/man1/
	cp -fp *.5 $(INSTALLROOT)$(MANROOT)/man5/
	cp -fp *.8 $(INSTALLROOT)$(MANROOT)/man8/
	cd $(INSTALLROOT)$(MANROOT)/man8 && ln -sf fping.8 fping4.8 && ln -sf fping.8 fping6.8

