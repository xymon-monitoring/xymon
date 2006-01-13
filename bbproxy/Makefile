# bbproxy Makefile

PROGRAMS = bbproxy bbmessage.cgi

PROXYOBJS = bbproxy.o
BBCGIOBJS = bbmessage.o

all: $(PROGRAMS)

bbproxy: $(PROXYOBJS)
	$(CC) $(CFLAGS) -o $@ $(PROXYOBJS) ../lib/libbbgen.a $(NETLIBS)

bbmessage.cgi: $(BBCGIOBJS) ../lib/libbbgen.a
	$(CC) $(CFLAGS) -o $@ $(BBCGIOBJS) ../lib/libbbgen.a $(NETLIBS)

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

