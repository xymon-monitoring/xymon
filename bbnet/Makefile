# Hobbit - BBNET Makefile

# ARES settings. c-ares is included
ARESTGZ = c-ares-$(ARESVER).tar.gz
DNSFLAGS = -I./$(ARESDIR)

PROGRAMS = bbtest-net
EXTENSIONS = bbretest-net.sh
CONFIGFILES = bb-services

all: $(PROGRAMS) $(EXTENSIONS)

NETTESTOBJS = bbtest-net.o contest.o httptest.o httpresult.o ldaptest.o dns.o dns2.o

bbtest-net: $(NETTESTOBJS) ../lib/libbbgen.a
	$(CC) $(CFLAGS) -o $@ $(NETTESTOBJS) ../lib/libbbgen.a libcares.a $(LDAPLIBS) $(SSLLIBS) $(NETLIBS)


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

libcares.a: $(ARESDIR)/.libs/libcares.a
	cp $(ARESDIR)/.libs/libcares.a .

$(ARESDIR)/.libs/libcares.a: $(ARESDIR)/Makefile
	(cd $(ARESDIR) && $(MAKE))

$(ARESDIR)/Makefile: $(ARESDIR)/configure
	(cd $(ARESDIR) && ./configure --disable-shared)

$(ARESDIR)/configure: $(ARESTGZ)
	gzip -dc $(ARESTGZ) | tar xf -

################################################
# Default compilation rules
################################################
%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

%.sh: %.sh.DIST
	cat $< | sed -e 's!@BBHOME@!$(BBHOME)!g' >$@
	chmod 755 $@

clean:
	rm -f *.o *.a *~ bbtest-net bbretest-net.sh


install: install-bin install-ext install-config install-man

install-bin: $(PROGRAMS)
	cp -fp $(PROGRAMS) $(BBHOME)/bin/

install-ext: $(EXTENSIONS)
	cp -fp $(EXTENSIONS) $(BBHOME)/ext/

install-config:
	cp -fp $(CONFIGFILES) $(BBHOME)/etc/

install-man:
	mkdir -p $(MANROOT)/man1 $(MANROOT)/man5
	cp -fp *.1 $(MANROOT)/man1/
	cp -fp *.5 $(MANROOT)/man5/

