# bbgen - common tools
#

PROGRAMS = bbhostgrep bbhostshow bbdigest bb hobbitlaunch bbcmd

HOSTGREPOBJS = bbhostgrep.o
HOSTSHOWOBJS = bbhostshow.o
DIGESTOBJS = bbdigest.o
MSGOBJS = sendmsg-std.o
LAUNCHOBJS = hobbitlaunch.o
CMDOBJS = bbcmd.o

all: $(PROGRAMS)


bbhostgrep: $(HOSTGREPOBJS) ../lib/libbbgen.a
	$(CC) $(CFLAGS) -o $@ $(HOSTGREPOBJS) ../lib/libbbgen.a

bbhostshow: $(HOSTSHOWOBJS) ../lib/libbbgen.a
	$(CC) $(CFLAGS) -o $@ $(HOSTSHOWOBJS) ../lib/libbbgen.a

bbdigest: $(DIGESTOBJS) ../lib/libbbgen.a
	$(CC) $(CFLAGS) -o $@ $(DIGESTOBJS) ../lib/libbbgen.a $(SSLLIBS) $(NETLIBS)

bb: $(MSGOBJS) ../lib/libbbgen.a
	$(CC) $(CFLAGS) -o $@ $(MSGOBJS) ../lib/libbbgen.a $(NETLIBS)

hobbitlaunch: $(LAUNCHOBJS) ../lib/libbbgen.a
	$(CC) $(CFLAGS) -o $@ $(LAUNCHOBJS) ../lib/libbbgen.a

bbcmd: $(CMDOBJS) ../lib/libbbgen.a
	$(CC) $(CFLAGS) -o $@ $(CMDOBJS) ../lib/libbbgen.a

sendmsg-std.o: ../lib/sendmsg.c
	$(CC) $(CFLAGS) -DSTANDALONE -c -o $@ ../lib/sendmsg.c


################################################
# Default compilation rules
################################################
%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f *.o *.a *~ $(PROGRAMS) $(CGISCRIPTS)

install: install-bin install-man

install-bin: $(PROGRAMS)
	cp -fp $(PROGRAMS) $(BBHOME)/bin/

install-man:
	mkdir -p $(MANROOT)/man1 $(MANROOT)/man5 $(MANROOT)/man7 $(MANROOT)/man8
	cp -fp *.1 $(MANROOT)/man1/
	cp -fp *.5 $(MANROOT)/man5/
	cp -fp *.7 $(MANROOT)/man7/
	cp -fp *.8 $(MANROOT)/man8/

