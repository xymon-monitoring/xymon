# bbgen - bbproxy Makefile

PROGRAMS = bbproxy

PROXYOBJS = bbproxy.o

all: $(PROGRAMS)

bbproxy: $(PROXYOBJS)
	$(CC) $(CFLAGS) -o $@ $(PROXYOBJS) ../lib/libbbgen.a $(NETLIBS)

################################################
# Default compilation rules
################################################
%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f *.o *.a *~ bbproxy

install: install-bin install-man

install-bin:
	cp -fp $(PROGRAMS) $(BBHOME)/bin/

install-man:
	mkdir -p $(MANROOT)/man1
	cp -fp *.1 $(MANROOT)/man1/

