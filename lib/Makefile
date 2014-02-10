# Xymon library Makefile
#

XYMONLIBOBJS = osdefs.o acklog.o availability.o calc.o cgi.o cgiurls.o clientlocal.o color.o crondate.o digest.o encoding.o environ.o errormsg.o eventlog.o files.o headfoot.o xymonrrd.o holidays.o htmllog.o ipaccess.o loadalerts.o loadcriticalconf.o links.o matching.o md5.o memory.o misc.o msort.o netservices.o notifylog.o readmib.o reportlog.o rmd160c.o sha1.o sha2.o sig.o stackio.o strfunc.o suid.o timefunc.o tree.o url.o webaccess.o

XYMONCOMMLIBOBJS = $(XYMONLIBOBJS) loadhosts.o locator.o sendmsg.o xymond_ipc.o xymond_buffer.o
XYMONTIMELIBOBJS = run.o timing.o

CLIENTLIBOBJS = osdefs.o cgiurls.o color-client.o crondate.o digest.o encoding.o environ-client.o errormsg.o holidays.o ipaccess.o md5.o memory.o misc.o msort.o rmd160c.o sha1.o sha2.o sig.o stackio.o strfunc.o suid.o timefunc-client.o tree.o
ifeq ($(LOCALCLIENT),yes)
	CLIENTLIBOBJS += matching.o
endif
XYMONCLIENTCOMMLIBOBJS = locator.o loadhosts.o sendmsg.o xymond_ipc.o xymond_buffer.o

XYMONCLIENTLIB = libxymonclient.a
XYMONCLIENTLIBS = $(XYMONCLIENTLIB)
XYMONCLIENTCOMMLIB = libxymonclientcomm.a
XYMONCLIENTCOMMLIBS = $(XYMONCLIENTCOMMLIB) $(ZLIBLIBS) $(XYMONCLIENTLIB) $(SSLLIBS) $(NETLIBS) $(LIBRTDEF)

XYMONLIB = libxymon.a
XYMONLIBS = $(XYMONLIB)
XYMONCOMMLIB = libxymoncomm.a
XYMONCOMMLIBS = $(XYMONCOMMLIB) $(ZLIBLIBS) $(SSLLIBS) $(NETLIBS) $(LIBRTDEF)
XYMONTIMELIB = libxymontime.a
XYMONTIMELIBS = $(XYMONTIMELIB) $(LIBRTDEF)


CFLAGS += -I../include 

all: test-endianness $(XYMONLIB) $(XYMONCOMMLIB) $(XYMONTIMELIB) $(XYMONCLIENTCOMMLIB) $(XYMONCLIENTLIB) loadhosts stackio availability md5 sha1 rmd160 locator tree

client: test-endianness $(XYMONCLIENTLIB) $(XYMONCLIENTCOMMLIB) $(XYMONTIMELIB)

test-endianness: test-endianness.c
	$(CC) $(CFLAGS) -o $@ $<

$(XYMONLIB): $(XYMONLIBOBJS)
	ar cr $(XYMONLIB) $(XYMONLIBOBJS)
	ranlib $(XYMONLIB) || echo ""

$(XYMONCOMMLIB): $(XYMONCOMMLIBOBJS)
	ar cr $(XYMONCOMMLIB) $(XYMONCOMMLIBOBJS)
	ranlib $(XYMONCOMMLIB) || echo ""

$(XYMONTIMELIB): $(XYMONTIMELIBOBJS)
	ar cr $(XYMONTIMELIB) $(XYMONTIMELIBOBJS)
	ranlib $(XYMONTIMELIB) || echo ""

$(XYMONCLIENTLIB): $(CLIENTLIBOBJS)
	ar cr $(XYMONCLIENTLIB) $(CLIENTLIBOBJS)
	ranlib $(XYMONCLIENTLIB) || echo ""

$(XYMONCLIENTCOMMLIB): $(XYMONCLIENTCOMMLIBOBJS)
	ar cr $(XYMONCLIENTCOMMLIB) $(XYMONCLIENTCOMMLIBOBJS)
	ranlib $(XYMONCLIENTCOMMLIB) || echo ""

loadhosts.o: loadhosts.c loadhosts_file.c loadhosts_net.c
	$(CC) $(CFLAGS) -c -o $@ loadhosts.c

eventlog.o: eventlog.c
	$(CC) $(CFLAGS) $(PCREINCDIR) -c -o $@ $<

notifylog.o: notifylog.c
	$(CC) $(CFLAGS) $(PCREINCDIR) -c -o $@ $<

headfoot.o: headfoot.c
	$(CC) $(CFLAGS) $(PCREINCDIR) -c -o $@ $<

loadalerts.o: loadalerts.c
	$(CC) $(CFLAGS) $(PCREINCDIR) -c -o $@ $<

matching.o: matching.c
	$(CC) $(CFLAGS) $(PCREINCDIR) -c -o $@ $<

sha1.o: sha1.c
	$(CC) $(CFLAGS) `./test-endianness` -c -o $@ $<

rmd160c.o: rmd160c.c
	$(CC) $(CFLAGS) `./test-endianness` -c -o $@ $<

environ.o: environ.c
	$(CC) $(CFLAGS) -DXYMONTOPDIR=\"$(XYMONTOPDIR)\" -DXYMONLOGDIR=\"$(XYMONLOGDIR)\" -DXYMONHOSTNAME=\"$(XYMONHOSTNAME)\" -DXYMONHOSTIP=\"$(XYMONHOSTIP)\" -DXYMONHOSTOS=\"$(XYMONHOSTOS)\" -DXYMONHOME=\"$(XYMONHOME)\" -c -o $@ environ.c

environ-client.o: environ.c
	$(CC) $(CFLAGS) -DXYMONTOPDIR=\"$(XYMONTOPDIR)\" -DXYMONLOGDIR=\"$(XYMONLOGDIR)\" -DXYMONHOSTNAME=\"$(XYMONHOSTNAME)\" -DXYMONHOSTIP=\"$(XYMONHOSTIP)\" -DXYMONHOSTOS=\"$(XYMONHOSTOS)\" -DXYMONHOME=\"$(XYMONCLIENTHOME)\" -c -o $@ environ.c

color-client.o: color.c
	$(CC) $(CFLAGS) -DCLIENTONLY -c -o $@ $<

timefunc-client.o: timefunc.c
	$(CC) $(CFLAGS) -DCLIENTONLY -c -o $@ $<

loadhosts: loadhosts.c loadhosts_file.c loadhosts_net.c $(XYMONCOMMLIB)
	$(CC) $(CFLAGS) -DSTANDALONE -o $@ loadhosts.c $(XYMONCOMMLIBS)

stackio: stackio.c libxymon.a
	$(CC) $(CFLAGS) -DSTANDALONE -o $@ stackio.c $(XYMONLIBS)

availability: availability.c libxymon.a
	$(CC) $(CFLAGS) -DSTANDALONE -o $@ availability.c $(XYMONLIBS)

md5: md5.c
	$(CC) $(CFLAGS) -DSTANDALONE `./test-endianness` -o $@ md5.c

sha1: sha1.c
	$(CC) $(CFLAGS) -DSTANDALONE `./test-endianness` -o $@ sha1.c

rmd160: rmd160c.c
	$(CC) $(CFLAGS) -DSTANDALONE `./test-endianness` -o $@ rmd160c.c

locator: locator.c
	$(CC) $(CFLAGS) -DSTANDALONE -o $@ locator.c $(XYMONCOMMLIBS) $(XYMONLIBS)

tree: tree.c
	$(CC) $(CFLAGS) -DSTANDALONE -o $@ tree.c

clean:
	rm -f *.o *.a *~ loadhosts stackio availability test-endianness md5 sha1 rmd160 locator tree

