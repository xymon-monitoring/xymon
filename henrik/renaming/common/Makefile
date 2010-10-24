# bbgen - common tools
#

PROGRAMS = bbhostgrep bbdigest bb bbcmd hobbitlaunch xymoncfg
CLIENTPROGRAMS = ../client/bb ../client/hobbitlaunch ../client/bbcmd ../client/bbhostgrep ../client/xymoncfg ../client/bbdigest

HOSTGREPOBJS = bbhostgrep.o
HOSTSHOWOBJS = xymoncfg.o
DIGESTOBJS   = bbdigest.o
BBOBJS       = bb.o
LAUNCHOBJS   = hobbitlaunch.o
CMDOBJS      = bbcmd.o

all: $(PROGRAMS)

client: $(CLIENTPROGRAMS)

bbhostgrep: $(HOSTGREPOBJS) ../lib/libbbgen.a
	$(CC) $(CFLAGS) -o $@ $(HOSTGREPOBJS) ../lib/libbbgen.a $(NETLIBS) $(LIBRTDEF)

../client/bbhostgrep: $(HOSTGREPOBJS) ../lib/hobbitclient.a
	$(CC) $(CFLAGS) -o $@ $(HOSTGREPOBJS) ../lib/hobbitclient.a $(NETLIBS) $(LIBRTDEF)

xymoncfg: $(HOSTSHOWOBJS) ../lib/libbbgen.a
	$(CC) $(CFLAGS) -o $@ $(HOSTSHOWOBJS) ../lib/libbbgen.a $(LIBRTDEF)

../client/xymoncfg: $(HOSTSHOWOBJS) ../lib/hobbitclient.a
	$(CC) $(CFLAGS) -o $@ $(HOSTSHOWOBJS) ../lib/hobbitclient.a $(LIBRTDEF)

bb: $(BBOBJS) ../lib/libbbgen.a
	$(CC) $(CFLAGS) -o $@ $(BBOBJS) ../lib/libbbgen.a $(NETLIBS) $(LIBRTDEF)

../client/bb: $(BBOBJS) ../lib/hobbitclient.a
	$(CC) $(CFLAGS) -o $@ $(BBOBJS) ../lib/hobbitclient.a $(NETLIBS) $(LIBRTDEF)

hobbitlaunch: $(LAUNCHOBJS) ../lib/libbbgen.a
	$(CC) $(CFLAGS) -o $@ $(LAUNCHOBJS) ../lib/libbbgen.a $(LIBRTDEF)

../client/hobbitlaunch: $(LAUNCHOBJS) ../lib/hobbitclient.a
	$(CC) $(CFLAGS) -o $@ $(LAUNCHOBJS) ../lib/hobbitclient.a $(LIBRTDEF)

bbcmd: $(CMDOBJS) ../lib/libbbgen.a
	$(CC) $(CFLAGS) -o $@ $(CMDOBJS) ../lib/libbbgen.a $(LIBRTDEF)

../client/bbcmd: $(CMDOBJS) ../lib/hobbitclient.a
	$(CC) $(CFLAGS) -o $@ $(CMDOBJS) ../lib/hobbitclient.a $(LIBRTDEF)

bbdigest: $(DIGESTOBJS) ../lib/libbbgen.a
	$(CC) $(CFLAGS) -o $@ $(DIGESTOBJS) ../lib/libbbgen.a $(NETLIBS) $(LIBRTDEF)

../client/bbdigest: $(DIGESTOBJS) ../lib/hobbitclient.a
	$(CC) $(CFLAGS) -o $@ $(DIGESTOBJS) ../lib/hobbitclient.a $(NETLIBS) $(LIBRTDEF)


bb.exe: bb.c ../lib/strfunc.c ../lib/errormsg.c ../lib/environ.c ../lib/stackio.c ../lib/timefunc.c ../lib/memory.c ../lib/sendmsg.c ../lib/holidays.c ../lib/rbtr.c ../lib/msort.c
	$(CC) $(CFLAGS) -c bb.c 
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
	ar cr bbwin32.a environ.o strfunc.o errormsg.o stackio.o timefunc.o memory.o sendmsg.o holidays.o rbtr.o msort.o misc.o
	ranlib bbwin32.a || echo ""
	$(CC) -o $@ bb.o bbwin32.a


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

