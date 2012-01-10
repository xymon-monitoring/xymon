# Xymon - xymonnet Makefile

XYMONLIB = ../lib/libxymon.a
XYMONLIBS = $(XYMONLIB)
XYMONCOMMLIB = ../lib/libxymoncomm.a
XYMONCOMMLIBS = $(XYMONCOMMLIB) $(SSLLIBS) $(NETLIBS) $(LIBRTDEF)
XYMONTIMELIB = ../lib/libxymontime.a
XYMONTIMELIBS = $(XYMONTIMELIB) $(LIBRTDEF)

# ARES settings. c-ares is included
DNSFLAGS = -I./c-ares

// PROGRAMS = xymonnet xymonping beastat
PROGRAMS = xymonnet2 tcptalk
EXTENSIONS = xymonnet-again.sh
SNMPPROGRAMS = xymon-snmpcollect
// DBGTOOLS = contest

ifeq ($(DOSNMP),yes)
PROGRAMS += $(SNMPPROGRAMS)
endif
all: $(PROGRAMS) $(EXTENSIONS) $(DBGTOOLS)

// NETTESTOBJS = xymonnet.o tcptalk.o ntptalk.o contest.o httptest.o httpresult.o ldaptest.o dns.o dns2.o httpcookies.o
NETTESTOBJS = xymonnet2.o setuptests.o netdialog.o tcptalk.o ntptalk.o dnstalk.o httpcookies.o dnsbits.o
TCPTALKOBJS = tcptalk-standalone.o ntptalk.o dnstalk.o httpcookies.o dnsbits.o
PINGTESTOBJS = xymonping.o
BEASTATOBJS = beastat.o


xymonnet2: libcares.a $(NETTESTOBJS) $(XYMONCOMMLIB) $(XYMONTIMELIB) $(XYMONLIB)
	$(CC) $(CFLAGS) -o $@ $(RPATHOPT) $(NETTESTOBJS) libcares.a $(XYMONTIMELIBS) $(XYMONCOMMLIBS) $(PCRELIBS) -lsqlite3

tcptalk: libcares.a $(TCPTALKOBJS) $(XYMONTIMELIB) $(XYMONCOMMLIB) libcares.a $(XYMONTIMELIB) $(XYMONLIB)
	$(CC) $(CFLAGS) -o $@ $(RPATHOPT) $(TCPTALKOBJS) libcares.a $(XYMONTIMELIBS) $(XYMONCOMMLIBS) $(PCRELIBS) -lsqlite3

tcptalk.o: tcptalk.c tcptalk.h dnstalk.h ntptalk.h
	$(CC) $(CFLAGS) -c -o $@ tcptalk.c

tcptalk-standalone.o: tcptalk.c tcptalk.h dnstalk.h ntptalk.h
	$(CC) $(CFLAGS) -DSTANDALONE -c -o $@ tcptalk.c

dnstalk.o: dnstalk.c dnstalk.h
	$(CC) $(CFLAGS) $(DNSFLAGS) -c -o $@ dnstalk.c

dnsbits.o: dnsbits.c dnsbits.h
	$(CC) $(CFLAGS) $(DNSFLAGS) -c -o $@ dnsbits.c

xymonping: $(PINGTESTOBJS) $(XYMONLIB) $(XYMONTIMELIB)
	$(CC) $(CFLAGS) -o $@ $(PINGTESTOBJS) $(XYMONTIMELIBS) $(XYMONLIBS) $(NETLIBS)

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

dns.o: libcares.a dns.c
	$(CC) $(CFLAGS) $(DNSFLAGS) -c -o $@ dns.c

dns2.o: dns2.c
	$(CC) $(CFLAGS) $(DNSFLAGS) -c -o $@ dns2.c

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

beastat: $(BEASTATOBJS) $(XYMONLIB) $(XYMONTIMELIB) $(XYMONCOMMLIB)
	$(CC) $(CFLAGS) -o $@ $(RPATHOPT) $(BEASTATOBJS) $(XYMONCOMMLIBS) $(XYMONTIMELIBS) $(XYMONLIBS) $(PCRELIBS)

beastat.o: beastat.c
	$(CC) $(CFLAGS) $(PCREINCDIR) -c -o $@ beastat.c


contest: contest.c httptest.o dns.o dns2.o httpcookies.o $(LIBOBJS)
	$(CC) $(CFLAGS) -o contest -I../include -Ic-ares -DSTANDALONE contest.c httptest.o dns.o dns2.o httpcookies.o ./libcares.a $(XYMONCOMMLIBS) $(XYMONTIMELIBS) $(XYMONLIBS)


xymon-snmpcollect: xymon-snmpcollect.o $(LIBOBJS)
	$(CC) $(LDFLAGS) -o $@ xymon-snmpcollect.o `net-snmp-config --libs` $(XYMONCOMMLIBS) $(XYMONLIBS) $(SSLLIBS)

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
	cp -fp $(PROGRAMS) $(INSTALLROOT)$(INSTALLBINDIR)/

install-ext: $(EXTENSIONS)
	cp -fp $(EXTENSIONS) $(INSTALLROOT)$(INSTALLEXTDIR)/

install-config:
	../build/convert-bbservices $(INSTALLROOT)$(INSTALLETCDIR)/protocols.cfg
	../build/merge-sects protocols.cfg $(INSTALLROOT)$(INSTALLETCDIR)/protocols.cfg

install-man:
	mkdir -p $(INSTALLROOT)$(MANROOT)/man1 $(INSTALLROOT)$(MANROOT)/man5
	cp -fp *.1 $(INSTALLROOT)$(MANROOT)/man1/
	cp -fp *.5 $(INSTALLROOT)$(MANROOT)/man5/

