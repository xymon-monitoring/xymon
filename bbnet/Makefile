# bbgen - BBNET Makefile

# ARES settings. c-ares 1.2.0 included with bbgen
DNSFLAGS = -I./c-ares-1.2.0

PROGRAMS = bbtest-net
EXTENSIONS = bbretest-net.sh
CONFIGFILES = bb-services

all: $(PROGRAMS) $(EXTENSIONS)

NETTESTOBJS = bbtest-net.o contest.o httptest.o httpresult.o ldaptest.o digest.o dns.o dns2.o

bbtest-net: $(NETTESTOBJS) 
	$(CC) $(CFLAGS) -o $@ $(NETTESTOBJS) $(LDAPLIBS) $(SSLLIBS) ../lib/libbbgen.a libcares.a $(NETLIBS)


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

digest.o: digest.c
	$(CC) $(CFLAGS) $(SSLFLAGS) $(SSLINCDIR) -c -o $@ digest.c

dns.o: libcares.a dns.c
	$(CC) $(CFLAGS) $(DNSFLAGS) -c -o $@ dns.c

dns2.o: dns2.c
	$(CC) $(CFLAGS) $(DNSFLAGS) -c -o $@ dns2.c

libcares.a: c-ares-1.2.0/libcares.a
	cp c-ares-1.2.0/libcares.a .

c-ares-1.2.0/libcares.a: c-ares-1.2.0/Makefile
	(cd c-ares-1.2.0 && make libcares.a)

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
	cp -p $(PROGRAMS) $(BBHOME)/bin/

install-ext: $(EXTENSIONS)
	cp -p $(EXTENSIONS) $(BBHOME)/ext/

install-config:
	cp -p $(CONFIGFILES) $(BBHOME)/etc/

install-man:
	mkdir -p $(MANROOT)/man{1,5}
	cp -p *.1 $(MANROOT)/man1/
	cp -p *.5 $(MANROOT)/man5/

