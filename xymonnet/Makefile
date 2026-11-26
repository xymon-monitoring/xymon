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


install: install-bin install-ext install-config install-man

install-bin: $(PROGRAMS)
ifndef PKGBUILD
	chown -h $(XYMONUSER) $(PROGRAMS)
	chgrp -h `$(IDTOOL) -g $(XYMONUSER)` $(PROGRAMS)
	chmod 755 $(PROGRAMS)
endif
	# Remove each destination name first: $(INSTALLBINDIR) belongs to $(XYMONUSER),
	# so a name there may be a symlink cp -fp would follow and write through.
	for f in $(PROGRAMS); do rm -f $(INSTALLROOT)$(INSTALLBINDIR)/$$f; done
	cp -fp $(PROGRAMS) $(INSTALLROOT)$(INSTALLBINDIR)/

install-ext: $(EXTENSIONS)
ifndef PKGBUILD
	chown -h $(XYMONUSER) $(EXTENSIONS)
	chgrp -h `$(IDTOOL) -g $(XYMONUSER)` $(EXTENSIONS)
	chmod 755 $(EXTENSIONS)
endif
	for e in $(EXTENSIONS); do rm -f $(INSTALLROOT)$(INSTALLEXTDIR)/$$e; done
	cp -fp $(EXTENSIONS) $(INSTALLROOT)$(INSTALLEXTDIR)/

# The debug tool "all" builds, xymonnetprobe -- contest.c built standalone,
# to run one of xymonnet's network tests by hand -- is otherwise discarded:
# nothing installs it, exactly like lib/'s diagnostics. Reached from the
# top-level "make install-tools"; deliberately not part of "install".
install-tools: $(DBGTOOLS)
	@mkdir -p $(INSTALLROOT)$(INSTALLBINDIR)
	rm -f $(INSTALLROOT)$(INSTALLBINDIR)/xymonnetprobe
	cp -fp xymonnetprobe $(INSTALLROOT)$(INSTALLBINDIR)/xymonnetprobe
	chmod 755 $(INSTALLROOT)$(INSTALLBINDIR)/xymonnetprobe
ifndef PKGBUILD
	chown -h $(XYMONUSER) $(INSTALLROOT)$(INSTALLBINDIR)/xymonnetprobe
	chgrp -h `$(IDTOOL) -g $(XYMONUSER)` $(INSTALLROOT)$(INSTALLBINDIR)/xymonnetprobe
endif

install-config:
	# protocols.cfg may be an admin-edited real file. merge-sects reads the name
	# (to merge in new sections) following a symlink there, copying the link's
	# target into the .bak; it unlinks the name before writing, so the write does
	# not follow. Drop a symlink here so the read does not either.
	if test -L $(INSTALLROOT)$(INSTALLETCDIR)/protocols.cfg; then rm -f $(INSTALLROOT)$(INSTALLETCDIR)/protocols.cfg; fi
	../build/merge-sects protocols.cfg $(INSTALLROOT)$(INSTALLETCDIR)/protocols.cfg
ifndef PKGBUILD
	chown -h $(XYMONUSER) $(INSTALLROOT)$(INSTALLETCDIR)/protocols.cfg
	chgrp -h `$(IDTOOL) -g $(XYMONUSER)` $(INSTALLROOT)$(INSTALLETCDIR)/protocols.cfg
	chmod 644 $(INSTALLROOT)$(INSTALLETCDIR)/protocols.cfg
endif

install-man:
ifndef PKGBUILD
	chown -h $(XYMONUSER) *.1 *.5
	chgrp -h `$(IDTOOL) -g $(XYMONUSER)` *.1 *.5
	chmod 644 *.1 *.5
endif
	mkdir -p $(INSTALLROOT)$(MANROOT)/man1 $(INSTALLROOT)$(MANROOT)/man5
ifndef PKGBUILD
	chown -h $(XYMONUSER) $(INSTALLROOT)$(MANROOT)/man1 $(INSTALLROOT)$(MANROOT)/man5
	chgrp -h `$(IDTOOL) -g $(XYMONUSER)` $(INSTALLROOT)$(MANROOT)/man1 $(INSTALLROOT)$(MANROOT)/man5
	chmod 755 $(INSTALLROOT)$(MANROOT)/man1 $(INSTALLROOT)$(MANROOT)/man5
endif
	for m in *.1; do rm -f $(INSTALLROOT)$(MANROOT)/man1/$$m; done
	cp -fp *.1 $(INSTALLROOT)$(MANROOT)/man1/
	for m in *.5; do rm -f $(INSTALLROOT)$(MANROOT)/man5/$$m; done
	cp -fp *.5 $(INSTALLROOT)$(MANROOT)/man5/

