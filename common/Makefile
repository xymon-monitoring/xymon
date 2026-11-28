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
XYMONCLIENTLIBS = $(XYMONCLIENTLIB)
XYMONCLIENTCOMMLIB = ../lib/libxymonclientcomm.a
XYMONCLIENTCOMMLIBS = $(XYMONCLIENTCOMMLIB) $(SSLLIBS) $(NETLIBS) $(LIBRTDEF)

XYMONLIB = ../lib/libxymon.a
XYMONLIBS = $(XYMONLIB)
XYMONCOMMLIB = ../lib/libxymoncomm.a
XYMONCOMMLIBS = $(XYMONCOMMLIB) $(SSLLIBS) $(NETLIBS) $(ZLIBLIBS) $(LIBRTDEF)
XYMONTIMELIB = ../lib/libxymontime.a
XYMONTIMELIBS = $(XYMONTIMELIB) $(LIBRTDEF)

all: $(PROGRAMS)

client: $(CLIENTPROGRAMS)

xymongrep: $(HOSTGREPOBJS) $(XYMONCOMMLIB) $(XYMONLIB)
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $(HOSTGREPOBJS) $(XYMONCOMMLIBS) $(XYMONLIBS)

../client/xymongrep: $(HOSTGREPOBJS) $(XYMONCLIENTCOMMLIB) $(XYMONCLIENTLIB)
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $(HOSTGREPOBJS) $(XYMONCLIENTCOMMLIBS) $(XYMONCLIENTLIBS)

xymoncfg: $(HOSTSHOWOBJS) $(XYMONLIB)
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $(HOSTSHOWOBJS) $(XYMONLIBS)

../client/xymoncfg: $(HOSTSHOWOBJS) $(XYMONCLIENTLIB)
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $(HOSTSHOWOBJS) $(XYMONCLIENTLIBS)

xymon: $(XYMONOBJS) $(XYMONCOMMLIB) $(XYMONLIB)
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $(XYMONOBJS) $(XYMONCOMMLIBS) $(XYMONLIBS)

../client/xymon: $(XYMONOBJS) $(XYMONCLIENTCOMMLIB) $(XYMONCLIENTLIB)
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $(XYMONOBJS) $(XYMONCLIENTCOMMLIBS) $(XYMONCLIENTLIBS)

xymonlaunch: $(LAUNCHOBJS) $(XYMONTIMELIB) $(XYMONLIB)
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $(LAUNCHOBJS) $(XYMONTIMELIBS) $(XYMONLIBS)

../client/xymonlaunch: $(LAUNCHOBJS) $(XYMONTIMELIB) $(XYMONCLIENTLIB)
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $(LAUNCHOBJS) $(XYMONTIMELIBS) $(XYMONCLIENTLIBS)

xymoncmd: $(CMDOBJS) $(XYMONLIB)
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $(CMDOBJS) $(XYMONLIBS)

../client/xymoncmd: $(CMDOBJS) $(XYMONCLIENTLIB)
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $(CMDOBJS) $(XYMONCLIENTLIBS)

xymondigest: $(DIGESTOBJS) $(XYMONLIB)
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $(DIGESTOBJS) $(XYMONCOMMLIBS) $(XYMONLIBS)

../client/xymondigest: $(DIGESTOBJS) $(XYMONCLIENTCOMMLIB) $(XYMONCLIENTLIB)
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $(DIGESTOBJS) $(XYMONCLIENTCOMMLIBS) $(XYMONCLIENTLIBS)


xymon.exe: xymon.c ../lib/strfunc.c ../lib/errormsg.c ../lib/environ.c ../lib/stackio.c ../lib/timefunc.c ../lib/memory.c ../lib/sendmsg.c ../lib/holidays.c ../lib/rbtr.c ../lib/msort.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c xymon.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -DXYMONTOPDIR=\"$(XYMONTOPDIR)\" -DXYMONLOGDIR=\"$(XYMONLOGDIR)\" -DXYMONHOSTNAME=\"$(XYMONHOSTNAME)\" -DXYMONHOSTIP=\"$(XYMONHOSTIP)\" -DXYMONHOSTOS=\"$(XYMONHOSTOS)\" -DXYMONHOME=\"$(XYMONHOME)\" -c ../lib/environ.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c ../lib/strfunc.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c ../lib/errormsg.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c ../lib/stackio.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c ../lib/timefunc.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c ../lib/memory.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c ../lib/sendmsg.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c ../lib/holidays.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c ../lib/rbtr.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c ../lib/msort.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c ../lib/misc.c
	ar cr xymonwin32.a environ.o strfunc.o errormsg.o stackio.o timefunc.o memory.o sendmsg.o holidays.o rbtr.o msort.o misc.o
	ranlib xymonwin32.a || echo ""
	$(CC) $(LDFLAGS) -o $@ xymon.o xymonwin32.a


################################################
# Default compilation rules
################################################
%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

clean:
	rm -f *.o *.a *~ $(PROGRAMS) $(CLIENTPROGRAMS)

# install-safe places each file and directory without following a symlink at
# the destination name, owning and moding it as it goes. See client/Makefile.
INSTALLSAFE = ../build/install-safe
ifndef PKGBUILD
ISOWN = -o $(XYMONUSER) -g `$(IDTOOL) -g $(XYMONUSER)`
endif

install: install-bin install-man

install-bin: $(PROGRAMS)
	for f in $(PROGRAMS); do $(INSTALLSAFE) copy -m 755 $(ISOWN) $$f $(INSTALLROOT)$(INSTALLBINDIR)/$$f || exit 1; done
	# Compatibility symlinks for the old BigBrother command names. Creating a
	# symlink is not following one, so these stay as ln -s.
	cd $(INSTALLROOT)$(INSTALLBINDIR)/; rm -f bb bbcmd bbhostgrep bbhostshow; ln -s xymon bb; ln -s xymoncmd bbcmd; ln -s xymongrep bbhostgrep; ln -s xymondigest bbdigest; ln -s xymoncfg bbhostshow

install-man:
	for d in man1 man5 man7 man8; do $(INSTALLSAFE) mkdir -m 755 $(ISOWN) $(INSTALLROOT)$(MANROOT)/$$d || exit 1; done
	for m in *.1; do $(INSTALLSAFE) copy -m 644 $(ISOWN) $$m $(INSTALLROOT)$(MANROOT)/man1/$$m || exit 1; done
	for m in *.5; do $(INSTALLSAFE) copy -m 644 $(ISOWN) $$m $(INSTALLROOT)$(MANROOT)/man5/$$m || exit 1; done
	for m in *.7; do $(INSTALLSAFE) copy -m 644 $(ISOWN) $$m $(INSTALLROOT)$(MANROOT)/man7/$$m || exit 1; done
	for m in *.8; do $(INSTALLSAFE) copy -m 644 $(ISOWN) $$m $(INSTALLROOT)$(MANROOT)/man8/$$m || exit 1; done

