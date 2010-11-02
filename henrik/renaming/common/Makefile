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

all: $(PROGRAMS)

client: $(CLIENTPROGRAMS)

xymongrep: $(HOSTGREPOBJS) ../lib/libxymon.a
	$(CC) $(CFLAGS) -o $@ $(HOSTGREPOBJS) ../lib/libxymon.a $(NETLIBS) $(LIBRTDEF)

../client/xymongrep: $(HOSTGREPOBJS) ../lib/xymonclient.a
	$(CC) $(CFLAGS) -o $@ $(HOSTGREPOBJS) ../lib/xymonclient.a $(NETLIBS) $(LIBRTDEF)

xymoncfg: $(HOSTSHOWOBJS) ../lib/libxymon.a
	$(CC) $(CFLAGS) -o $@ $(HOSTSHOWOBJS) ../lib/libxymon.a $(LIBRTDEF)

../client/xymoncfg: $(HOSTSHOWOBJS) ../lib/xymonclient.a
	$(CC) $(CFLAGS) -o $@ $(HOSTSHOWOBJS) ../lib/xymonclient.a $(LIBRTDEF)

xymon: $(XYMONOBJS) ../lib/libxymon.a
	$(CC) $(CFLAGS) -o $@ $(XYMONOBJS) ../lib/libxymon.a $(NETLIBS) $(LIBRTDEF)

../client/xymon: $(XYMONOBJS) ../lib/xymonclient.a
	$(CC) $(CFLAGS) -o $@ $(XYMONOBJS) ../lib/xymonclient.a $(NETLIBS) $(LIBRTDEF)

xymonlaunch: $(LAUNCHOBJS) ../lib/libxymon.a
	$(CC) $(CFLAGS) -o $@ $(LAUNCHOBJS) ../lib/libxymon.a $(LIBRTDEF)

../client/xymonlaunch: $(LAUNCHOBJS) ../lib/xymonclient.a
	$(CC) $(CFLAGS) -o $@ $(LAUNCHOBJS) ../lib/xymonclient.a $(LIBRTDEF)

xymoncmd: $(CMDOBJS) ../lib/libxymon.a
	$(CC) $(CFLAGS) -o $@ $(CMDOBJS) ../lib/libxymon.a $(LIBRTDEF)

../client/xymoncmd: $(CMDOBJS) ../lib/xymonclient.a
	$(CC) $(CFLAGS) -o $@ $(CMDOBJS) ../lib/xymonclient.a $(LIBRTDEF)

xymondigest: $(DIGESTOBJS) ../lib/libxymon.a
	$(CC) $(CFLAGS) -o $@ $(DIGESTOBJS) ../lib/libxymon.a $(NETLIBS) $(LIBRTDEF)

../client/xymondigest: $(DIGESTOBJS) ../lib/xymonclient.a
	$(CC) $(CFLAGS) -o $@ $(DIGESTOBJS) ../lib/xymonclient.a $(NETLIBS) $(LIBRTDEF)


xymon.exe: xymon.c ../lib/strfunc.c ../lib/errormsg.c ../lib/environ.c ../lib/stackio.c ../lib/timefunc.c ../lib/memory.c ../lib/sendmsg.c ../lib/holidays.c ../lib/rbtr.c ../lib/msort.c
	$(CC) $(CFLAGS) -c xymon.c 
	$(CC) $(CFLAGS) -DBBTOPDIR=\"$(BBTOPDIR)\" -DBBLOGDIR=\"$(BBLOGDIR)\" -DBBHOSTNAME=\"$(BBHOSTNAME)\" -DBBHOSTIP=\"$(BBHOSTIP)\" -DBBHOSTOS=\"$(BBHOSTOS)\" -DBUILD_HOME=\"$(BBTOPDIR)/client\" -c ../lib/environ.c
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

install-man:
	mkdir -p $(INSTALLROOT)$(MANROOT)/man1 $(INSTALLROOT)$(MANROOT)/man5 $(INSTALLROOT)$(MANROOT)/man7 $(INSTALLROOT)$(MANROOT)/man8
	cp -fp *.1 $(INSTALLROOT)$(MANROOT)/man1/
	cp -fp *.5 $(INSTALLROOT)$(MANROOT)/man5/
	cp -fp *.7 $(INSTALLROOT)$(MANROOT)/man7/
	cp -fp *.8 $(INSTALLROOT)$(MANROOT)/man8/

