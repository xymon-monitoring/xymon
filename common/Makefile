# bbgen - common tools
#

PROGRAMS = bbhostgrep bbhostshow bbdigest bb hobbitlaunch bbcmd bbmessage.cgi
CLIENTPROGRAMS = ../client/bb ../client/hobbitlaunch ../client/bbcmd ../client/bbhostgrep ../client/bbhostshow

HOSTGREPOBJS = bbhostgrep.o
HOSTSHOWOBJS = bbhostshow.o
DIGESTOBJS = bbdigest.o
MSGOBJS = sendmsg-std.o
MSGCGIOBJS = sendmsg-cgi.o
LAUNCHOBJS = hobbitlaunch.o
CMDOBJS = bbcmd.o

all: $(PROGRAMS)

client: $(CLIENTPROGRAMS)

bbhostgrep: $(HOSTGREPOBJS) ../lib/libbbgen.a
	$(CC) $(CFLAGS) -o $@ $(HOSTGREPOBJS) ../lib/libbbgen.a

../client/bbhostgrep: $(HOSTGREPOBJS) ../lib/hobbitclient.a
	$(CC) $(CFLAGS) -o $@ $(HOSTGREPOBJS) ../lib/hobbitclient.a

bbhostshow: $(HOSTSHOWOBJS) ../lib/libbbgen.a
	$(CC) $(CFLAGS) -o $@ $(HOSTSHOWOBJS) ../lib/libbbgen.a

../client/bbhostshow: $(HOSTSHOWOBJS) ../lib/hobbitclient.a
	$(CC) $(CFLAGS) -o $@ $(HOSTSHOWOBJS) ../lib/hobbitclient.a

bb: $(MSGOBJS) ../lib/libbbgen.a
	$(CC) $(CFLAGS) -o $@ $(MSGOBJS) ../lib/libbbgen.a $(NETLIBS)

../client/bb: $(MSGOBJS) ../lib/hobbitclient.a
	$(CC) $(CFLAGS) -o $@ $(MSGOBJS) ../lib/hobbitclient.a $(NETLIBS)

hobbitlaunch: $(LAUNCHOBJS) ../lib/libbbgen.a
	$(CC) $(CFLAGS) -o $@ $(LAUNCHOBJS) ../lib/libbbgen.a

../client/hobbitlaunch: $(LAUNCHOBJS) ../lib/hobbitclient.a
	$(CC) $(CFLAGS) -o $@ $(LAUNCHOBJS) ../lib/hobbitclient.a

bbcmd: $(CMDOBJS) ../lib/libbbgen.a
	$(CC) $(CFLAGS) -o $@ $(CMDOBJS) ../lib/libbbgen.a

../client/bbcmd: $(CMDOBJS) ../lib/hobbitclient.a
	$(CC) $(CFLAGS) -o $@ $(CMDOBJS) ../lib/hobbitclient.a

bbdigest: $(DIGESTOBJS) ../lib/libbbgen.a
	$(CC) $(CFLAGS) -o $@ $(RPATHOPT) $(DIGESTOBJS) ../lib/libbbgen.a $(SSLLIBS) $(NETLIBS)

bbmessage.cgi: $(MSGCGIOBJS) ../lib/libbbgen.a
	$(CC) $(CFLAGS) -o $@ $(MSGCGIOBJS) ../lib/libbbgen.a $(NETLIBS)

sendmsg-std.o: ../lib/sendmsg.c
	$(CC) $(CFLAGS) -DSTANDALONE -c -o $@ ../lib/sendmsg.c

sendmsg-cgi.o: ../lib/sendmsg.c
	$(CC) $(CFLAGS) -DCGI -c -o $@ ../lib/sendmsg.c

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

