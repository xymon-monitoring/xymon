# bbgen - BBNET Makefile

# ARES settings. c-ares 1.2.0 included with bbgen
DNSFLAGS = -I./c-ares-1.2.0

PROGRAMS = bbtest-net
EXTENSIONS = bbretest-net.sh
CONFIGFILES = bb-services

all: $(PROGRAMS) $(EXTENSIONS)

NETTESTOBJS = bbtest-net.o contest.o httptest.o httpresult.o ldaptest.o dns.o dns2.o

bbtest-net: $(NETTESTOBJS) 
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

libcares.a: c-ares-1.2.0/libcares.a
	cp c-ares-1.2.0/libcares.a .

c-ares-1.2.0/libcares.a: c-ares-1.2.0/Makefile
	(cd c-ares-1.2.0 && $(MAKE) libcares.a)

c-ares-1.2.0/Makefile:
	(cd c-ares-1.2.0 && ./configure)

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

