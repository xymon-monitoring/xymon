# Xymon library Makefile
#

XYMONLIBOBJS = osdefs.o acklog.o availability.o calc.o cgi.o cgiurls.o clientlocal.o color.o compression.o crondate.o digest.o encoding.o environ.o errormsg.o eventlog.o files.o headfoot.o xymonrrd.o holidays.o htmllog.o ipaccess.o loadalerts.o loadcriticalconf.o links.o matching.o md5.o memory.o misc.o msort.o netservices.o notifylog.o acknowledgementslog.o readmib.o reportlog.o rmd160c.o sha1.o sha2.o sig.o stackio.o stdopt.o strfunc.o suid.o timefunc.o tree.o url.o webaccess.o

XYMONCOMMLIBOBJS = $(XYMONLIBOBJS) compression.o loadhosts.o locator.o minilzo.o sendmsg.o tcplib.o xymond_ipc.o xymond_buffer.o
XYMONTIMELIBOBJS = run.o timing.o

CLIENTLIBOBJS = osdefs.o cgiurls.o color-client.o crondate.o digest.o encoding.o environ-client.o errormsg.o holidays.o ipaccess.o md5.o memory.o misc.o msort.o rmd160c.o sha1.o sha2.o sig.o stackio.o stdopt.o strfunc.o suid.o tcplib.o timefunc-client.o tree.o url.o
ifeq ($(LOCALCLIENT),yes)
	CLIENTLIBOBJS += matching.o
endif
XYMONCLIENTCOMMLIBOBJS = compression.o locator.o loadhosts.o minilzo.o sendmsg.o xymond_ipc.o xymond_buffer.o

XYMONCLIENTLIB = libxymonclient
XYMONCLIENTLIBS = -lxymonclient
XYMONCLIENTCOMMLIB = libxymonclientcomm
XYMONCLIENTCOMMLIBS = -lxymonclientcomm $(COMPLIBS) $(SSLLIBS) $(NETLIBS) $(LIBRTDEF)

XYMONLIB = libxymon
XYMONLIBS = -lxymon
XYMONCOMMLIB = libxymoncomm
XYMONCOMMLIBS = -lxymoncomm $(NETLIBS) $(LIBRTDEF) $(COMPLIBS) $(SSLLIBS) $(PCRELIBS)
XYMONTIMELIB = libxymontime
XYMONTIMELIBS = -lxymontime $(LIBRTDEF)


CFLAGS += -I../include -L../lib
ifeq ($(ENABLESHARED),yes)
	CFLAGS += -fpic
	# SONAME = ".0"
	# LIBVERSION = ".0.0.0"
endif

all: test-endianness $(XYMONLIB) $(XYMONCOMMLIB) $(XYMONTIMELIB) $(XYMONCLIENTCOMMLIB) $(XYMONCLIENTLIB) loadhosts stackio availability md5 sha1 rmd160 locator tree

client: test-endianness $(XYMONCLIENTLIB) $(XYMONCLIENTCOMMLIB) $(XYMONTIMELIB)

test-endianness: test-endianness.c
	$(CC) $(CFLAGS) -o $@ $<

$(XYMONLIB): $(XYMONLIBOBJS)
	ar cr $(XYMONLIB).a $(XYMONLIBOBJS)
	ranlib $(XYMONLIB).a || echo ""
	test x"$(ENABLESHARED)" = x"yes" && $(CC) $(CFLAGS) $(LDFLAGS) -shared -Wl,-soname,$(XYMONLIB).so$(SONAME) -o $(XYMONLIB).so$(LIBVERSION) $(XYMONLIBOBJS) || :

$(XYMONCOMMLIB): $(XYMONCOMMLIBOBJS)
	ar cr $(XYMONCOMMLIB).a $(XYMONCOMMLIBOBJS)
	ranlib $(XYMONCOMMLIB).a || echo ""
	test x"$(ENABLESHARED)" = x"yes" && $(CC) $(CFLAGS) $(LDFLAGS) -shared -Wl,-soname,$(XYMONCOMMLIB).so$(SONAME) -o $(XYMONCOMMLIB).so$(LIBVERSION) $(XYMONCOMMLIBOBJS) || :

$(XYMONTIMELIB): $(XYMONTIMELIBOBJS)
	ar cr $(XYMONTIMELIB).a $(XYMONTIMELIBOBJS)
	ranlib $(XYMONTIMELIB).a || echo ""
	test x"$(ENABLESHARED)" = x"yes" && $(CC) $(CFLAGS) $(LDFLAGS) -shared -Wl,-soname,$(XYMONTIMELIB).so$(SONAME) -o $(XYMONTIMELIB).so$(LIBVERSION) $(XYMONTIMELIBOBJS) || :

$(XYMONCLIENTLIB): $(CLIENTLIBOBJS)
	ar cr $(XYMONCLIENTLIB).a $(CLIENTLIBOBJS)
	ranlib $(XYMONCLIENTLIB).a || echo ""
	test x"$(ENABLESHARED)" = x"yes" && $(CC) $(CFLAGS) $(LDFLAGS) -shared -Wl,-soname,$(XYMONCLIENTLIB).so$(SONAME) -o $(XYMONCLIENTLIB).so$(LIBVERSION) $(CLIENTLIBOBJS) || :

$(XYMONCLIENTCOMMLIB): $(XYMONCLIENTCOMMLIBOBJS)
	ar cr $(XYMONCLIENTCOMMLIB).a $(XYMONCLIENTCOMMLIBOBJS)
	ranlib $(XYMONCLIENTCOMMLIB).a || echo ""
	test x"$(ENABLESHARED)" = x"yes" && $(CC) $(CFLAGS) $(LDFLAGS) -shared -Wl,-soname,$(XYMONCLIENTCOMMLIB).so$(SONAME) -o $(XYMONCLIENTCOMMLIB).so$(LIBVERSION) $(XYMONCLIENTCOMMLIBOBJS) || :

loadhosts.o: loadhosts.c loadhosts_file.c loadhosts_net.c
	$(CC) $(CFLAGS) $(PCREINCDIR) -c -o $@ loadhosts.c

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

sha1.o: sha1.c
	$(CC) $(CFLAGS) `./test-endianness` -c -o $@ $<

minilzo.o: minilzo.c
	$(CC) $(CFLAGS) -s -fomit-frame-pointer `./test-endianness` -c -o $@ $<

rmd160c.o: rmd160c.c
	$(CC) $(CFLAGS) `./test-endianness` -c -o $@ $<

tcplib.o: tcplib.c
	$(CC) $(CFLAGS) $(SSLFLAGS) -c -o $@ $<

environ.o: environ.c
	$(CC) $(CFLAGS) -DXYMONTOPDIR=\"$(XYMONTOPDIR)\" -DXYMONLOGDIR=\"$(XYMONLOGDIR)\" -DXYMONHOSTNAME=\"$(XYMONHOSTNAME)\" -DXYMONHOSTIP=\"$(XYMONHOSTIP)\" -DXYMONHOSTOS=\"$(XYMONHOSTOS)\" -DXYMONHOME=\"$(XYMONHOME)\" -c -o $@ environ.c

environ-client.o: environ.c
	$(CC) $(CFLAGS) -DXYMONTOPDIR=\"$(XYMONTOPDIR)\" -DXYMONLOGDIR=\"$(XYMONLOGDIR)\" -DXYMONHOSTNAME=\"$(XYMONHOSTNAME)\" -DXYMONHOSTIP=\"$(XYMONHOSTIP)\" -DXYMONHOSTOS=\"$(XYMONHOSTOS)\" -DXYMONHOME=\"$(XYMONCLIENTHOME)\" -c -o $@ environ.c

color-client.o: color.c
	$(CC) $(CFLAGS) -DCLIENTONLY -c -o $@ $<

timefunc-client.o: timefunc.c
	$(CC) $(CFLAGS) -DCLIENTONLY -c -o $@ $<

loadhosts: loadhosts.c loadhosts_file.c loadhosts_net.c $(XYMONLIB) $(XYMONCOMMLIB)
	$(CC) $(CFLAGS) -DSTANDALONE -o $@ loadhosts.c $(XYMONLIBS) $(XYMONCOMMLIBS)

stackio: stackio.c $(XYMONLIB) $(XYMONCOMMLIB)
	$(CC) $(CFLAGS) -DSTANDALONE -o $@ stackio.c $(XYMONLIBS) $(XYMONCOMMLIBS)

availability: availability.c $(XYMONLIB) $(XYMONCOMMLIB)
	$(CC) $(CFLAGS) -DSTANDALONE -o $@ availability.c $(XYMONLIBS) $(XYMONCOMMLIBS)

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
	rm -f *.o *.a *.so *.so.* *~ loadhosts stackio availability test-endianness md5 sha1 rmd160 locator tree

install:
	cp -fp *.so* *.a $(INSTALLROOT)$(INSTALLLIBDIR)/ || :
	chmod 644 $(INSTALLROOT)$(INSTALLLIBDIR)/*.a || :
	chmod 755 $(INSTALLROOT)$(INSTALLLIBDIR)/*.so* || :
