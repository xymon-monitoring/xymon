# Hobbit - BBNET Makefile

# ARES settings. c-ares is included
DNSFLAGS = -I./c-ares

PROGRAMS = bbtest-net hobbitping beastat
SNMPPROGRAMS = hobbit_snmpcollect
EXTENSIONS = bbretest-net.sh
DBGTOOLS = contest

ifeq ($(DOSNMP),yes)
PROGRAMS += $(SNMPPROGRAMS)
endif

LIBOBJS = ../lib/$(HOBBITLIBRARY)

all: $(PROGRAMS) $(EXTENSIONS) $(DBGTOOLS)

NETTESTOBJS = bbtest-net.o contest.o httptest.o httpresult.o ldaptest.o dns.o dns2.o httpcookies.o
PINGTESTOBJS = hobbitping.o
BEASTATOBJS = beastat.o

bbtest-net: $(NETTESTOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(RPATHOPT) $(NETTESTOBJS) $(LIBOBJS) libcares.a $(LDAPLIBS) $(SSLLIBS) $(PCRELIBS) $(HOBBITZLIB) $(NETLIBS)

hobbitping: $(PINGTESTOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(PINGTESTOBJS) $(LIBOBJS) $(NETLIBS)

bbtest-net.o: bbtest-net.c
	$(CC) $(CFLAGS) $(SSLFLAGS) $(LDAPFLAGS) $(SSLINCDIR) $(LDAPINCDIR) -c -o $@ bbtest-net.c

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

beastat: $(BEASTATOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(RPATHOPT) $(BEASTATOBJS) $(LIBOBJS) $(PCRELIBS) $(HOBBITZLIB) $(NETLIBS)

beastat.o: beastat.c
	$(CC) $(CFLAGS) $(PCREINCDIR) -c -o $@ beastat.c


contest: contest.c $(LIBOBJS)
	$(CC) $(CFLAGS) -o contest -I../include -Ic-ares -DSTANDALONE contest.c httptest.o dns.o dns2.o ./libcares.a $(LIBOBJS) $(HOBBITZLIB) $(NETLIBS)


hobbit_snmpcollect: hobbit_snmpcollect.o $(LIBOBJS)
	$(CC) $(LDFLAGS) -o $@ hobbit_snmpcollect.o `net-snmp-config --libs` $(LIBOBJS) $(HOBBITZLIB) $(NETLIBS)

hobbit_snmpcollect.o: hobbit_snmpcollect.c
	$(CC) $(CFLAGS) -I. `net-snmp-config --cflags` -c -o $@ hobbit_snmpcollect.c

################################################
# Default compilation rules
################################################
%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

%.sh: %.sh.DIST
	cat $< | sed -e 's!@BBHOME@!$(BBHOME)!g' | sed -e 's!@RUNTIMEDEFS@!$(RUNTIMEDEFS)!g' >$@
	chmod 755 $@

clean:
	rm -f *.o *.a *~ $(PROGRAMS) $(SNMPPROGRAMS) $(EXTENSIONS) $(DBGTOOLS)


install: install-bin install-ext install-config install-man

install-bin: $(PROGRAMS)
	cp -fp $(PROGRAMS) $(INSTALLROOT)$(INSTALLBINDIR)/

install-ext: $(EXTENSIONS)
	cp -fp $(EXTENSIONS) $(INSTALLROOT)$(INSTALLEXTDIR)/

install-config:
	../build/convert-bbservices $(INSTALLROOT)$(INSTALLETCDIR)/bb-services
	../build/merge-sects bb-services $(INSTALLROOT)$(INSTALLETCDIR)/bb-services

install-man:
	mkdir -p $(INSTALLROOT)$(MANROOT)/man1 $(INSTALLROOT)$(MANROOT)/man5
	cp -fp *.1 $(INSTALLROOT)$(MANROOT)/man1/
	cp -fp *.5 $(INSTALLROOT)$(MANROOT)/man5/

