# Xymon library Makefile
#

XYMONLIBOBJS = osdefs.o acklog.o availability.o calc.o cgi.o cgiurls.o clientlocal.o color.o crondate.o digest.o encoding.o environ.o errormsg.o eventlog.o files.o headfoot.o xymonrrd.o holidays.o htmllog.o ipaccess.o loadalerts.o loadcriticalconf.o links.o matching.o md5.o memory.o misc.o msort.o netservices.o notifylog.o acknowledgementslog.o readmib.o reportlog.o rmd160c.o sha1.o sha2.o sig.o stackio.o strfunc.o suid.o timefunc.o tree.o url.o webaccess.o

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

acknowledgementslog.o: acknowledgementslog.c
	$(CC) $(CFLAGS) $(PCREINCDIR) -c -o $@ $<

headfoot.o: headfoot.c
	$(CC) $(CFLAGS) $(PCREINCDIR) -c -o $@ $<

loadalerts.o: loadalerts.c
	$(CC) $(CFLAGS) $(PCREINCDIR) -c -o $@ $<

matching.o: matching.c
	$(CC) $(CFLAGS) $(PCREINCDIR) -c -o $@ $<

sha1.o: sha1.c test-endianness
	$(CC) $(CFLAGS) `./test-endianness` -c -o $@ $<

rmd160c.o: rmd160c.c test-endianness
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

md5: md5.c test-endianness
	$(CC) $(CFLAGS) -DSTANDALONE `./test-endianness` -o $@ md5.c

sha1: sha1.c test-endianness
	$(CC) $(CFLAGS) -DSTANDALONE `./test-endianness` -o $@ sha1.c

rmd160: rmd160c.c test-endianness
	$(CC) $(CFLAGS) -DSTANDALONE `./test-endianness` -o $@ rmd160c.c

locator: locator.c $(XYMONCOMMLIB) $(XYMONLIB)
	$(CC) $(CFLAGS) -DSTANDALONE -o $@ locator.c $(XYMONCOMMLIBS) $(XYMONLIBS)

tree: tree.c
	$(CC) $(CFLAGS) -DSTANDALONE -o $@ tree.c

# The diagnostic programs built by "all" are otherwise discarded with the
# build tree: nothing installs them, so anyone who wants stackio to check
# how a config file parses, or loadhosts to see what hosts.cfg resolves
# to, has to keep a build tree around or copy them out by hand. Every
# packaging does the latter.
#
# Deliberately not part of "install": these are diagnostics, and adding
# five binaries to what every installation gets is a decision for whoever
# runs the install, not a side effect of this change. Packagers and
# anyone who wants them can ask:
#
#	make install-tools
#
LIBTOOLS = loadhosts stackio availability locator tree

install-tools: $(LIBTOOLS)
	@mkdir -p $(INSTALLROOT)$(INSTALLBINDIR)
	for f in $(LIBTOOLS); do \
		cp -fp $$f $(INSTALLROOT)$(INSTALLBINDIR)/ || exit 1; \
		chmod 755 $(INSTALLROOT)$(INSTALLBINDIR)/$$f || exit 1; \
	done

clean:
	rm -f *.o *.a *~ loadhosts stackio availability test-endianness md5 sha1 rmd160 locator tree
