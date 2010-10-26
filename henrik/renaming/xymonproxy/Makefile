# xymonproxy Makefile

PROGRAMS = xymonproxy xymoncgimsg.cgi

PROXYOBJS = xymonproxy.o
BBCGIOBJS = xymoncgimsg.o

all: $(PROGRAMS)

xymonproxy: $(PROXYOBJS)
	$(CC) $(CFLAGS) -o $@ $(PROXYOBJS) ../lib/libxymon.a $(NETLIBS) $(LIBRTDEF)

xymoncgimsg.cgi: $(BBCGIOBJS) ../lib/libxymon.a
	$(CC) $(CFLAGS) -o $@ $(BBCGIOBJS) ../lib/libxymon.a $(NETLIBS) $(LIBRTDEF)

################################################
# Default compilation rules
################################################
%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f *.o *.a *~ $(PROGRAMS)

install: install-bin install-man

install-bin:
	cp -fp $(PROGRAMS) $(INSTALLROOT)$(INSTALLBINDIR)/

install-man:
	mkdir -p $(INSTALLROOT)$(MANROOT)/man8
	cp -fp *.8 $(INSTALLROOT)$(MANROOT)/man8/

