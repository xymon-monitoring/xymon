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
DBGTOOLS = xymonnetprobe


ifeq ($(DOSNMP),yes)
PROGRAMS += $(SNMPPROGRAMS)
endif
all: $(PROGRAMS) $(EXTENSIONS) $(DBGTOOLS)

NETTESTOBJS = xymonnet.o contest.o httptest.o httpresult.o ldaptest.o dns.o dns2.o httpcookies.o udpquery.o
PINGTESTOBJS = xymonping.o
BEASTATOBJS = beastat.o

xymonnet: $(NETTESTOBJS) $(XYMONCOMMLIB) $(XYMONTIMELIB) $(XYMONLIBS)
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $(RPATHOPT) $(NETTESTOBJS) $(CARESLIBS) $(LDAPLIBS) $(XYMONTIMELIBS) $(XYMONCOMMLIBS) $(XYMONLIBS) $(PCRELIBS)

xymonping: $(PINGTESTOBJS) $(XYMONTIMELIB) ../lib/libxymon.a
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $(PINGTESTOBJS) $(XYMONTIMELIBS) $(XYMONCOMMLIBS)

# The local headers each object includes: without them, changing a header
# such as xymonnet.h -- a struct layout -- leaves the other objects built
# against the old one. CMake works these out itself; make needs them listed.
xymonnet.o: xymonnet.h contest.h httptest.h dns.h ldaptest.h httpresult.h httpcookies.h
contest.o: xymonnet.h contest.h httptest.h dns.h
httptest.o: xymonnet.h contest.h httptest.h dns.h httpcookies.h
httpresult.o: xymonnet.h contest.h httpresult.h httpcookies.h
ldaptest.o: xymonnet.h ldaptest.h
dns.o: dns.h dns2.h
dns2.o: dns2.h
httpcookies.o: httpcookies.h
xymonnetprobe: xymonnet.h contest.h httptest.h dns.h

xymonnet.o: xymonnet.c ntpprobe.c udpquery.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SSLFLAGS) $(LDAPFLAGS) $(SSLINCDIR) $(LDAPINCDIR) -c -o $@ xymonnet.c

udpquery.o: udpquery.c udpquery.h

ldaptest.o: ldaptest.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDAPFLAGS) $(LDAPINCDIR) -c -o $@ ldaptest.c

httptest.o: httptest.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SSLFLAGS) $(SSLINCDIR) -c -o $@ httptest.c

httpresult.o: httpresult.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SSLFLAGS) $(SSLINCDIR) -c -o $@ httpresult.c

contest.o: contest.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SSLFLAGS) $(SSLINCDIR) -c -o $@ contest.c

dns.o: dns.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CARESINCDIR) -c -o $@ dns.c

dns2.o: dns2.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CARESINCDIR) -c -o $@ dns2.c

beastat: $(BEASTATOBJS) $(XYMONCOMMLIB) $(XYMONTIMELIB)
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $(RPATHOPT) $(BEASTATOBJS) $(XYMONCOMMLIBS) $(XYMONTIMELIBS) $(PCRELIBS)

beastat.o: beastat.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(PCREINCDIR) -c -o $@ beastat.c


xymonnetprobe: contest.c httptest.o dns.o dns2.o httpcookies.o $(XYMONCOMMLIB) $(XYMONTIMELIB)
	$(CC) $(LDFLAGS) $(CFLAGS) $(SSLFLAGS) $(SSLINCDIR) -o $@ -I../include $(CARESINCDIR) -DSTANDALONE contest.c httptest.o dns.o dns2.o httpcookies.o $(CARESLIBS) $(XYMONCOMMLIBS) $(XYMONTIMELIBS)


# SNMPINCDIR and SNMPLIBS come from configure (build/snmp.sh), which keeps
# only the interface flags from net-snmp-config's output - see the filtering
# note there.
xymon-snmpcollect: xymon-snmpcollect.o $(XYMONCOMMLIB) $(XYMONTIMELIB)
	$(CC) $(LDFLAGS) -o $@ xymon-snmpcollect.o $(SNMPLIBS) ../lib/libxymon.a $(XYMONCOMMLIBS) $(XYMONTIMELIBS)

xymon-snmpcollect.o: xymon-snmpcollect.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -I. $(SNMPINCDIR) -c -o $@ xymon-snmpcollect.c

################################################
# Default compilation rules
################################################
%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

%.sh: %.sh.DIST
	XYMONHOME='$(XYMONHOME)' RUNTIMEDEFS='$(RUNTIMEDEFS)' ../build/subst.sh XYMONHOME RUNTIMEDEFS <$< >$@
	chmod 755 $@

clean:
	rm -f *.o *.a *~ $(PROGRAMS) $(EXTENSIONS) $(DBGTOOLS)


# install-safe places each file and directory without following a symlink at
# the destination name, owning and moding it as it goes. See client/Makefile.
INSTALLSAFE = ../build/install-safe
ifndef PKGBUILD
ISOWN = -o $(XYMONUSER) -g `$(IDTOOL) -g $(XYMONUSER)`
endif

install: install-bin install-ext install-config install-man

install-bin: $(PROGRAMS)
	# xymonping needs raw sockets, so outside a package build it is installed
	# setuid-root -- as a fresh copy from the trusted build output, never by
	# chmod'ing the installed, Xymon-owned file in place (which that user could
	# replace first). A setuid mode is accepted only root-owned and not
	# group/world-writable (4755 is). Where the install is not run as root the
	# copy to root fails, and it falls back to a plain Xymon-owned install, as the
	# old best-effort chmod did. Under PKGBUILD the setuid bit is left to the
	# packaging, so a staged xymonping is installed plain like the rest.
	for f in $(PROGRAMS); do \
		if [ "$$f" = xymonping ] && [ -z "$(PKGBUILD)" ]; then \
			$(INSTALLSAFE) copy -m 4755 -o root $$f $(INSTALLROOT)$(INSTALLBINDIR)/$$f \
			|| $(INSTALLSAFE) copy -m 755 $(ISOWN) $$f $(INSTALLROOT)$(INSTALLBINDIR)/$$f \
			|| exit 1; \
		else \
			$(INSTALLSAFE) copy -m 755 $(ISOWN) $$f $(INSTALLROOT)$(INSTALLBINDIR)/$$f || exit 1; \
		fi; \
	done

install-ext: $(EXTENSIONS)
	for f in $(EXTENSIONS); do $(INSTALLSAFE) copy -m 755 $(ISOWN) $$f $(INSTALLROOT)$(INSTALLEXTDIR)/$$f || exit 1; done

# The debug tool "all" builds, xymonnetprobe -- contest.c built standalone,
# to run one of xymonnet's network tests by hand -- is otherwise discarded:
# nothing installs it, exactly like lib/'s diagnostics. Reached from the
# top-level "make install-tools"; deliberately not part of "install".
install-tools: $(DBGTOOLS)
	$(INSTALLSAFE) mkdir -m 755 $(INSTALLROOT)$(INSTALLBINDIR)
	$(INSTALLSAFE) copy -m 755 $(ISOWN) xymonnetprobe $(INSTALLROOT)$(INSTALLBINDIR)/xymonnetprobe

install-config:
	# protocols.cfg is installed over the old copy on every upgrade (it has no .d
	# drop-in). See releasenotes.d.
	d="$(INSTALLROOT)$(INSTALLETCDIR)/protocols.cfg"; \
	if [ -f "$$d" ] && [ ! -L "$$d" ]; then $(INSTALLSAFE) copy -m 600 "$$d" "$$d.bak" || exit 1; fi; \
	$(INSTALLSAFE) copy -m 644 $(ISOWN) protocols.cfg "$$d"

install-man:
	$(INSTALLSAFE) mkdir -m 755 $(ISOWN) $(INSTALLROOT)$(MANROOT)/man1
	$(INSTALLSAFE) mkdir -m 755 $(ISOWN) $(INSTALLROOT)$(MANROOT)/man5
	for m in *.1; do $(INSTALLSAFE) copy -m 644 $(ISOWN) $$m $(INSTALLROOT)$(MANROOT)/man1/$$m || exit 1; done
	for m in *.5; do $(INSTALLSAFE) copy -m 644 $(ISOWN) $$m $(INSTALLROOT)$(MANROOT)/man5/$$m || exit 1; done

