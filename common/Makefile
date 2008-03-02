# bbgen - common tools
#

PROGRAMS = bbhostgrep bbhostshow bbdigest bb bbcmd hobbitlaunch
CLIENTPROGRAMS = ../client/bb ../client/hobbitlaunch ../client/bbcmd ../client/bbhostgrep ../client/bbhostshow ../client/bbdigest

HOSTGREPOBJS = bbhostgrep.o
HOSTSHOWOBJS = bbhostshow.o
DIGESTOBJS   = bbdigest.o
BBOBJS       = bb.o
LAUNCHOBJS   = hobbitlaunch.o
CMDOBJS      = bbcmd.o

LIBOBJS = ../lib/$(HOBBITLIBRARY)

all: $(PROGRAMS)

client: $(CLIENTPROGRAMS)

bbhostgrep: $(HOSTGREPOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(HOSTGREPOBJS) $(LIBOBJS) $(HOBBITZLIB) $(NETLIBS)

../client/bbhostgrep: $(HOSTGREPOBJS) ../lib/hobbitclient.a
	$(CC) $(CFLAGS) -o $@ $(HOSTGREPOBJS) ../lib/hobbitclient.a $(HOBBITZLIB) $(NETLIBS)

bbhostshow: $(HOSTSHOWOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(HOSTSHOWOBJS) $(LIBOBJS) $(HOBBITZLIB)

../client/bbhostshow: $(HOSTSHOWOBJS) ../lib/hobbitclient.a
	$(CC) $(CFLAGS) -o $@ $(HOSTSHOWOBJS) ../lib/hobbitclient.a $(HOBBITZLIB)

bb: $(BBOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(BBOBJS) $(LIBOBJS) $(HOBBITZLIB) $(NETLIBS)

../client/bb: $(BBOBJS) ../lib/hobbitclient.a
	$(CC) $(CFLAGS) -o $@ $(BBOBJS) ../lib/hobbitclient.a $(HOBBITZLIB) $(NETLIBS)

hobbitlaunch: $(LAUNCHOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(LAUNCHOBJS) $(LIBOBJS) $(HOBBITZLIB)

../client/hobbitlaunch: $(LAUNCHOBJS) ../lib/hobbitclient.a
	$(CC) $(CFLAGS) -o $@ $(LAUNCHOBJS) ../lib/hobbitclient.a $(HOBBITZLIB)

bbcmd: $(CMDOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(CMDOBJS) $(LIBOBJS) $(HOBBITZLIB)

../client/bbcmd: $(CMDOBJS) ../lib/hobbitclient.a
	$(CC) $(CFLAGS) -o $@ $(CMDOBJS) ../lib/hobbitclient.a $(HOBBITZLIB)

bbdigest: $(DIGESTOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(DIGESTOBJS) $(LIBOBJS) $(HOBBITZLIB) $(NETLIBS)

../client/bbdigest: $(DIGESTOBJS) ../lib/hobbitclient.a
	$(CC) $(CFLAGS) -o $@ $(DIGESTOBJS) ../lib/hobbitclient.a $(HOBBITZLIB) $(NETLIBS)

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
	if test -f $(INSTALLROOT)$(INSTALLBINDIR)/bbz; then rm -f $(INSTALLROOT)$(INSTALLBINDIR)/bbz; fi
	cd $(INSTALLROOT)$(INSTALLBINDIR) && ln -s bb bbz

install-man:
	mkdir -p $(INSTALLROOT)$(MANROOT)/man1 $(INSTALLROOT)$(MANROOT)/man5 $(INSTALLROOT)$(MANROOT)/man7 $(INSTALLROOT)$(MANROOT)/man8
	cp -fp *.1 $(INSTALLROOT)$(MANROOT)/man1/
	cp -fp *.5 $(INSTALLROOT)$(MANROOT)/man5/
	cp -fp *.7 $(INSTALLROOT)$(MANROOT)/man7/
	cp -fp *.8 $(INSTALLROOT)$(MANROOT)/man8/

