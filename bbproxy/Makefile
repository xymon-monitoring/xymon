# bbproxy Makefile

PROGRAMS = bbproxy bbmessage.cgi

PROXYOBJS = bbproxy.o
BBCGIOBJS = bbmessage.o
LIBOBJS = ../lib/$(HOBBITLIBRARY)

all: $(PROGRAMS)

bbproxy: $(PROXYOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(PROXYOBJS) $(LIBOBJS) $(NETLIBS)

bbmessage.cgi: $(BBCGIOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(BBCGIOBJS) $(LIBOBJS) $(NETLIBS)

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

