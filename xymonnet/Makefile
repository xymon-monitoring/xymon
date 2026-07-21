# Xymon - xymonnet Makefile

XYMONLIB = ../lib/libxymon.a
XYMONLIBS = $(XYMONLIB)
XYMONCOMMLIB = ../lib/libxymoncomm.a
XYMONCOMMLIBS = $(XYMONCOMMLIB) $(ZLIBLIBS) $(SSLLIBS) $(NETLIBS) $(LIBRTDEF)
XYMONTIMELIB = ../lib/libxymontime.a
XYMONTIMELIBS = $(XYMONTIMELIB) $(LIBRTDEF)

# c-ares (async DNS) is a required system dependency; the formerly-bundled
# copy has been removed. configure provides CARESINCDIR and CARESLIBS.

PROGRAMS = xymonnet xymonping beastat
EXTENSIONS = xymonnet-again.sh
SNMPPROGRAMS = xymon-snmpcollect
DBGTOOLS = contest


ifeq ($(DOSNMP),yes)
PROGRAMS += $(SNMPPROGRAMS)
endif
all: $(PROGRAMS) $(EXTENSIONS) $(DBGTOOLS)

NETTESTOBJS = xymonnet.o contest.o httptest.o httpresult.o ldaptest.o dns.o dns2.o httpcookies.o udpquery.o
PINGTESTOBJS = xymonping.o
BEASTATOBJS = beastat.o

xymonnet: $(NETTESTOBJS) $(XYMONCOMMLIB) $(XYMONTIMELIB) $(XYMONLIBS)
	$(CC) $(CFLAGS) -o $@ $(RPATHOPT) $(NETTESTOBJS) $(CARESLIBS) $(LDAPLIBS) $(XYMONTIMELIBS) $(XYMONCOMMLIBS) $(XYMONLIBS) $(PCRELIBS)

xymonping: $(PINGTESTOBJS) $(XYMONTIMELIB) ../lib/libxymon.a
	$(CC) $(CFLAGS) -o $@ $(PINGTESTOBJS) $(XYMONTIMELIBS) $(XYMONCOMMLIBS)

xymonnet.o: xymonnet.c ntpprobe.c udpquery.h
	$(CC) $(CFLAGS) $(SSLFLAGS) $(LDAPFLAGS) $(SSLINCDIR) $(LDAPINCDIR) -c -o $@ xymonnet.c

udpquery.o: udpquery.c udpquery.h

ldaptest.o: ldaptest.c
	$(CC) $(CFLAGS) $(LDAPFLAGS) $(LDAPINCDIR) -c -o $@ ldaptest.c

httptest.o: httptest.c
	$(CC) $(CFLAGS) $(SSLFLAGS) $(SSLINCDIR) -c -o $@ httptest.c

httpresult.o: httpresult.c
	$(CC) $(CFLAGS) $(SSLFLAGS) $(SSLINCDIR) -c -o $@ httpresult.c

contest.o: contest.c
	$(CC) $(CFLAGS) $(SSLFLAGS) $(SSLINCDIR) -c -o $@ contest.c

dns.o: dns.c
	$(CC) $(CFLAGS) $(CARESINCDIR) -c -o $@ dns.c

dns2.o: dns2.c
	$(CC) $(CFLAGS) $(CARESINCDIR) -c -o $@ dns2.c

beastat: $(BEASTATOBJS) $(XYMONCOMMLIB) $(XYMONTIMELIB)
	$(CC) $(CFLAGS) -o $@ $(RPATHOPT) $(BEASTATOBJS) $(XYMONCOMMLIBS) $(XYMONTIMELIBS) $(PCRELIBS)

beastat.o: beastat.c
	$(CC) $(CFLAGS) $(PCREINCDIR) -c -o $@ beastat.c


contest: contest.c httptest.o dns.o dns2.o httpcookies.o $(XYMONCOMMLIB) $(XYMONTIMELIB)
	$(CC) $(CFLAGS) -o contest -I../include $(CARESINCDIR) -DSTANDALONE contest.c httptest.o dns.o dns2.o httpcookies.o $(CARESLIBS) $(XYMONCOMMLIBS) $(XYMONTIMELIBS)


# net-snmp-config exports the distro's full build policy along with the
# interface: -Werror=declaration-after-statement (newer net-snmp), the EL9
# hardened-ld -specs (forces PIE links against our non-PIC objects), LTO,
# fortify options, and more. Keep only what we actually need from it:
# for compiling, include dirs and defines; for linking, library names,
# dirs and runtime paths.
SNMPCFLAGS := $(shell for flag in `net-snmp-config --cflags 2>/dev/null`; do \
		case $$flag in \
			(-I*|-D*|-U*|-pthread) echo $$flag ;; \
		esac; \
	done)

SNMPLIBS := $(shell for flag in `net-snmp-config --libs 2>/dev/null`; do \
		case $$flag in \
			(-l*|-L*|-Wl,-rpath*|-Wl,-R*|-pthread) echo $$flag ;; \
		esac; \
	done)

xymon-snmpcollect: xymon-snmpcollect.o $(XYMONCOMMLIB) $(XYMONTIMELIB)
	$(CC) $(LDFLAGS) -o $@ xymon-snmpcollect.o $(SNMPLIBS) ../lib/libxymon.a $(XYMONCOMMLIBS) $(XYMONTIMELIBS)

xymon-snmpcollect.o: xymon-snmpcollect.c
	$(CC) $(CFLAGS) -I. $(SNMPCFLAGS) -c -o $@ xymon-snmpcollect.c

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

