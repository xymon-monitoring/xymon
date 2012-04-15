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
	$(CC) $(CFLAGS) -o $@ $(HOSTGREPOBJS) $(XYMONCOMMLIBS) $(XYMONLIBS)

../client/xymongrep: $(HOSTGREPOBJS) $(XYMONCLIENTCOMMLIB) $(XYMONCLIENTLIB)
	$(CC) $(CFLAGS) -o $@ $(HOSTGREPOBJS) $(XYMONCLIENTCOMMLIBS) $(XYMONCLIENTLIBS)

xymoncfg: $(HOSTSHOWOBJS) $(XYMONLIB)
	$(CC) $(CFLAGS) -o $@ $(HOSTSHOWOBJS) $(XYMONLIBS)

../client/xymoncfg: $(HOSTSHOWOBJS) $(XYMONCLIENTLIB)
	$(CC) $(CFLAGS) -o $@ $(HOSTSHOWOBJS) $(XYMONCLIENTLIBS)

xymon: $(XYMONOBJS) $(XYMONCOMMLIB) $(XYMONLIB)
	$(CC) $(CFLAGS) -o $@ $(XYMONOBJS) $(XYMONCOMMLIBS) $(XYMONLIBS)

../client/xymon: $(XYMONOBJS) $(XYMONCLIENTCOMMLIB) $(XYMONCLIENTLIB)
	$(CC) $(CFLAGS) -o $@ $(XYMONOBJS) $(XYMONCLIENTCOMMLIBS) $(XYMONCLIENTLIBS)

xymonlaunch: $(LAUNCHOBJS) $(XYMONTIMELIB) $(XYMONLIB)
	$(CC) $(CFLAGS) -o $@ $(LAUNCHOBJS) $(XYMONTIMELIBS) $(XYMONLIBS)

../client/xymonlaunch: $(LAUNCHOBJS) $(XYMONTIMELIB) $(XYMONCLIENTLIB)
	$(CC) $(CFLAGS) -o $@ $(LAUNCHOBJS) $(XYMONTIMELIBS) $(XYMONCLIENTLIBS)

xymoncmd: $(CMDOBJS) $(XYMONLIB)
	$(CC) $(CFLAGS) -o $@ $(CMDOBJS) $(XYMONLIBS)

../client/xymoncmd: $(CMDOBJS) $(XYMONCLIENTLIB)
	$(CC) $(CFLAGS) -o $@ $(CMDOBJS) $(XYMONCLIENTLIBS)

xymondigest: $(DIGESTOBJS) $(XYMONLIB)
	$(CC) $(CFLAGS) -o $@ $(DIGESTOBJS) $(XYMONCOMMLIBS) $(XYMONLIBS)

../client/xymondigest: $(DIGESTOBJS) $(XYMONCLIENTCOMMLIB) $(XYMONCLIENTLIB)
	$(CC) $(CFLAGS) -o $@ $(DIGESTOBJS) $(XYMONCLIENTCOMMLIBS) $(XYMONCLIENTLIBS)


xymon.exe: xymon.c ../lib/strfunc.c ../lib/errormsg.c ../lib/environ.c ../lib/stackio.c ../lib/timefunc.c ../lib/memory.c ../lib/sendmsg.c ../lib/holidays.c ../lib/rbtr.c ../lib/msort.c
	$(CC) $(CFLAGS) -c xymon.c 
	$(CC) $(CFLAGS) -DXYMONTOPDIR=\"$(XYMONTOPDIR)\" -DXYMONLOGDIR=\"$(XYMONLOGDIR)\" -DXYMONHOSTNAME=\"$(XYMONHOSTNAME)\" -DXYMONHOSTIP=\"$(XYMONHOSTIP)\" -DXYMONHOSTOS=\"$(XYMONHOSTOS)\" -DBUILD_HOME=\"$(XYMONTOPDIR)/client\" -c ../lib/environ.c
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

install: install-bin install-man

install-bin: $(PROGRAMS)
	cp -fp $(PROGRAMS) $(INSTALLROOT)$(INSTALLBINDIR)/
	cd $(INSTALLROOT)$(INSTALLBINDIR)/; rm -f bb bbcmd bbhostgrep bbhostshow; ln -s xymon bb; ln -s xymoncmd bbcmd; ln -s xymongrep bbhostgrep; ln -s xymondigest bbdigest; ln -s xymoncfg bbhostshow

install-man:
	mkdir -p $(INSTALLROOT)$(MANROOT)/man1 $(INSTALLROOT)$(MANROOT)/man5 $(INSTALLROOT)$(MANROOT)/man7 $(INSTALLROOT)$(MANROOT)/man8
	cp -fp *.1 $(INSTALLROOT)$(MANROOT)/man1/
	cp -fp *.5 $(INSTALLROOT)$(MANROOT)/man5/
	cp -fp *.7 $(INSTALLROOT)$(MANROOT)/man7/
	cp -fp *.8 $(INSTALLROOT)$(MANROOT)/man8/

