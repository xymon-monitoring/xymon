# Makefile for xymongen
#

PROGRAMS = xymongen
GENOBJS = xymongen.o loadlayout.o loaddata.o pagegen.o process.o wmlgen.o rssgen.o util.o debug.o csvreport.o

XYMONLIB = ../lib/libxymon.a
XYMONLIBS = $(XYMONLIB)
XYMONCOMMLIB = ../lib/libxymoncomm.a
XYMONCOMMLIBS = $(XYMONCOMMLIB) $(SSLLIBS) $(NETLIBS) $(LIBRTDEF)
XYMONTIMELIB = ../lib/libxymontime.a
XYMONTIMELIBS = $(XYMONTIMELIB) $(LIBRTDEF)

all: $(PROGRAMS)

xymongen: $(GENOBJS) $(XYMONTIMELIB) $(XYMONCOMMLIB)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(GENOBJS) $(XYMONTIMELIBS) $(XYMONCOMMLIBS) $(PCRELIBS)


################################################
# Default compilation rules
################################################
%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f *.o *.a *~ $(PROGRAMS)


install: install-bin install-man

install-bin: $(PROGRAMS)
	cp -fp $(PROGRAMS) $(INSTALLROOT)$(INSTALLBINDIR)/

install-man:
	mkdir -p $(INSTALLROOT)$(MANROOT)/man1 $(INSTALLROOT)$(MANROOT)/man5
	cp -fp *.1 $(INSTALLROOT)$(MANROOT)/man1/

