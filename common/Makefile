# Xymon - common tools
#

PROGRAMS = xymongrep xymondigest xymon xymoncmd xymonlaunch xymoncfg
CLIENTPROGRAMS = ../client/xymon ../client/xymonlaunch ../client/xymoncmd ../client/xymongrep ../client/xymoncfg ../client/xymondigest

HOSTGREPOBJS = xymongrep.o
HOSTSHOWOBJS = xymoncfg.o
DIGESTOBJS   = xymondigest.o
XYMONOBJS    = xymon.o
LAUNCHOBJS   = xymonlaunch.o
CMDOBJS      = xymoncmd.o

XYMONCLIENTLIB = ../lib/libxymonclient.a
XYMONCLIENTLIBS = -lxymonclient
XYMONCLIENTCOMMLIB = ../lib/libxymonclientcomm.a
XYMONCLIENTCOMMLIBS = -lxymonclientcomm $(COMPLIBS) $(SSLLIBS) $(NETLIBS) $(LIBRTDEF)

XYMONLIB = ../lib/libxymon.a
XYMONLIBS = -lxymon $(PCRELIBS)
XYMONCOMMLIB = ../lib/libxymoncomm.a
XYMONCOMMLIBS = -lxymoncomm $(SSLLIBS) $(NETLIBS) $(COMPLIBS) $(LIBRTDEF) $(PCRELIBS)
XYMONTIMELIB = ../lib/libxymontime.a
XYMONTIMELIBS = -lxymontime $(LIBRTDEF)

XYMONALLLIBS = $(XYMONCLIENTLIB) $(XYMONCLIENTCOMMLIB) $(XYMONLIB) $(XYMONCOMMLIB) $(XYMONTIMELIB)

all: $(PROGRAMS)

client: $(CLIENTPROGRAMS)

xymongrep: $(HOSTGREPOBJS) $(XYMONCOMMLIB) $(XYMONLIB)
	$(CC) $(CFLAGS) -o $@ $(HOSTGREPOBJS) $(XYMONCOMMLIBS) $(XYMONLIBS)

../client/xymongrep: $(HOSTGREPOBJS) $(XYMONCLIENTCOMMLIB) $(XYMONCLIENTLIB)
	$(CC) $(CFLAGS) -o $@ $(HOSTGREPOBJS) $(XYMONCLIENTCOMMLIBS) $(XYMONCLIENTLIBS)

xymoncfg: $(HOSTSHOWOBJS) $(XYMONCOMMLIB) $(XYMONLIB)
	$(CC) $(CFLAGS) -o $@ $(HOSTSHOWOBJS) $(XYMONCOMMLIBS) $(XYMONLIBS)

../client/xymoncfg: $(HOSTSHOWOBJS) $(XYMONCLIENTCOMMLIB) $(XYMONCLIENTLIB)
	$(CC) $(CFLAGS) -o $@ $(HOSTSHOWOBJS) $(XYMONCLIENTCOMMLIBS) $(XYMONCLIENTLIBS)

xymon: $(XYMONOBJS) $(XYMONCOMMLIB) $(XYMONLIB)
	$(CC) $(CFLAGS) -o $@ $(XYMONOBJS) $(XYMONCOMMLIBS) $(XYMONLIBS)

../client/xymon: $(XYMONOBJS) $(XYMONCLIENTCOMMLIB) $(XYMONCLIENTLIB)
	$(CC) $(CFLAGS) -o $@ $(XYMONOBJS) $(XYMONCLIENTCOMMLIBS) $(XYMONCLIENTLIBS)

xymonlaunch: $(LAUNCHOBJS) $(XYMONTIMELIB) $(XYMONCOMMLIB) $(XYMONLIB)
	$(CC) $(CFLAGS) -o $@ $(LAUNCHOBJS) $(XYMONTIMELIBS) $(XYMONCOMMLIBS) $(XYMONLIBS)

../client/xymonlaunch: $(LAUNCHOBJS) $(XYMONTIMELIB) $(XYMONCLIENTCOMMLIB) $(XYMONCLIENTLIB)
	$(CC) $(CFLAGS) -o $@ $(LAUNCHOBJS) $(XYMONTIMELIBS) $(XYMONCLIENTCOMMLIBS) $(XYMONCLIENTLIBS)

xymoncmd: $(CMDOBJS) $(XYMONCOMMLIB) $(XYMONLIB)
	$(CC) $(CFLAGS) -o $@ $(CMDOBJS) $(XYMONCOMMLIBS) $(XYMONLIBS)

../client/xymoncmd: $(CMDOBJS) $(XYMONCLIENTCOMMLIB) $(XYMONCLIENTLIB)
	$(CC) $(CFLAGS) -o $@ $(CMDOBJS) $(XYMONCLIENTCOMMLIBS) $(XYMONCLIENTLIBS)

xymondigest: $(DIGESTOBJS) $(XYMONLIB)
	$(CC) $(CFLAGS) -o $@ $(DIGESTOBJS) $(XYMONCOMMLIBS) $(XYMONLIBS)

../client/xymondigest: $(DIGESTOBJS) $(XYMONCLIENTCOMMLIB) $(XYMONCLIENTLIB)
	$(CC) $(CFLAGS) -o $@ $(DIGESTOBJS) $(XYMONCLIENTCOMMLIBS) $(XYMONCLIENTLIBS)


xymon.exe: xymon.c ../lib/strfunc.c ../lib/errormsg.c ../lib/environ.c ../lib/stackio.c ../lib/timefunc.c ../lib/memory.c ../lib/sendmsg.c ../lib/holidays.c ../lib/rbtr.c ../lib/msort.c
	$(CC) $(CFLAGS) -c xymon.c 
	$(CC) $(CFLAGS) -DXYMONTOPDIR=\"$(XYMONTOPDIR)\" -DXYMONLOGDIR=\"$(XYMONLOGDIR)\" -DXYMONHOSTNAME=\"$(XYMONHOSTNAME)\" -DXYMONHOSTIP=\"$(XYMONHOSTIP)\" -DXYMONHOSTOS=\"$(XYMONHOSTOS)\" -DXYMONHOME=\"$(XYMONHOME)\" -c ../lib/environ.c
	$(CC) $(CFLAGS) -c ../lib/strfunc.c 
	$(CC) $(CFLAGS) -c ../lib/errormsg.c
	$(CC) $(CFLAGS) -c ../lib/stackio.c 
	$(CC) $(CFLAGS) -c ../lib/timefunc.c 
	$(CC) $(CFLAGS) -c ../lib/memory.c 
	$(CC) $(CFLAGS) -c ../lib/sendmsg.c 
	$(CC) $(CFLAGS) -c ../lib/holidays.c 
	$(CC) $(CFLAGS) -c ../lib/rbtr.c 
	$(CC) $(CFLAGS) -c ../lib/msort.c
	$(CC) $(CFLAGS) -c ../lib/misc.c
	ar cr xymonwin32.a environ.o strfunc.o errormsg.o stackio.o timefunc.o memory.o sendmsg.o holidays.o rbtr.o msort.o misc.o
	ranlib xymonwin32.a || echo ""
	$(CC) -o $@ xymon.o xymonwin32.a


################################################
# Default compilation rules
################################################
%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f *.o *.a *~ $(PROGRAMS) $(CLIENTPROGRAMS)

install: install-bin install-man install-libs

install-bin: $(PROGRAMS)
ifndef PKGBUILD
	chown $(XYMONUSER) $(PROGRAMS)
	chgrp `$(IDTOOL) -g $(XYMONUSER)` $(PROGRAMS)
	chmod 755 $(PROGRAMS)
endif
	cp -fp $(PROGRAMS) $(INSTALLROOT)$(INSTALLBINDIR)/
	cd $(INSTALLROOT)$(INSTALLBINDIR)/; rm -f bb bbcmd bbhostgrep bbhostshow; ln -s xymon bb; ln -s xymoncmd bbcmd; ln -s xymongrep bbhostgrep; ln -s xymondigest bbdigest; ln -s xymoncfg bbhostshow

install-man:
ifndef PKGBUILD
	chown $(XYMONUSER) *.1 *.5 *.7 *.8
	chgrp `$(IDTOOL) -g $(XYMONUSER)` *.1 *.5 *.7 *.8
	chmod 644 *.1 *.5 *.7 *.8
endif
	mkdir -p $(INSTALLROOT)$(MANROOT)/man1 $(INSTALLROOT)$(MANROOT)/man5 $(INSTALLROOT)$(MANROOT)/man7 $(INSTALLROOT)$(MANROOT)/man8
ifndef PKGBUILD
	chown $(XYMONUSER) $(INSTALLROOT)$(MANROOT)/man1 $(INSTALLROOT)$(MANROOT)/man5 $(INSTALLROOT)$(MANROOT)/man7 $(INSTALLROOT)$(MANROOT)/man8
	chgrp `$(IDTOOL) -g $(XYMONUSER)` $(INSTALLROOT)$(MANROOT)/man1 $(INSTALLROOT)$(MANROOT)/man5 $(INSTALLROOT)$(MANROOT)/man7 $(INSTALLROOT)$(MANROOT)/man8
	chmod 755 $(INSTALLROOT)$(MANROOT)/man1 $(INSTALLROOT)$(MANROOT)/man5 $(INSTALLROOT)$(MANROOT)/man7 $(INSTALLROOT)$(MANROOT)/man8
endif
	cp -fp *.1 $(INSTALLROOT)$(MANROOT)/man1/
	cp -fp *.5 $(INSTALLROOT)$(MANROOT)/man5/
	cp -fp *.7 $(INSTALLROOT)$(MANROOT)/man7/
	cp -fp *.8 $(INSTALLROOT)$(MANROOT)/man8/

install-libs: $(XYMONALLLIBS)
ifdef PKGBUILD
	mkdir -p $(INSTALLROOT)$(INSTALLLIBDIR)/
	cp -fp $(XYMONALLLIBS) $(INSTALLROOT)$(INSTALLLIBDIR)/
	chmod 755 $(INSTALLROOT)$(INSTALLLIBDIR)/*.so*	|| :
	chmod 644 $(INSTALLROOT)$(INSTALLLIBDIR)/*.a	|| :
endif
	:
