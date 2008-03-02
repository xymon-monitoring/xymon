# Hobbit library Makefile
#

HOBBITLIBOBJS = osdefs.o acklog.o availability.o calc.o cgi.o cgiurls.o clientlocal.o color.o compression.o digest.o encoding.o environ.o errormsg.o eventlog.o files.o headfoot.o hobbitrrd.o holidays.o htmllog.o ipaccess.o loadalerts.o loadhosts.o loadnkconf.o locator.o links.o matching.o md5.o memory.o misc.o msort.o netservices.o notifylog.o rbtr.o readmib.o reportlog.o rmd160c.o sendmsg.o sha1.o sha2.o sig.o stackio.o strfunc.o suid.o timefunc.o timing.o url.o

CLIENTLIBOBJS = osdefs.o cgiurls.o color-client.o compression.o digest.o encoding.o environ-client.o errormsg.o holidays.o ipaccess.o loadhosts.o md5.o memory.o misc.o msort.o rbtr.o rmd160c.o sendmsg.o sha1.o sha2.o sig.o stackio.o strfunc.o suid.o timefunc-client.o
ifeq ($(LOCALCLIENT),yes)
	CLIENTLIBOBJS += matching.o
endif


CFLAGS += -I. -I../include

ifeq ($(HOBBITLIBRARY),libhobbit.so)
	CFLAGS += -fPIC
endif

HOBBITLIBSOVERSION = 1

all: test-endianness $(HOBBITLIBRARY) hobbitclient.a loadhosts stackio availability md5 sha1 rmd160 locator msort holidays

client: test-endianness hobbitclient.a

test-endianness: test-endianness.c
	$(CC) $(CFLAGS) -o $@ $<

libhobbit.a: $(HOBBITLIBOBJS)
	ar cr libhobbit.a $(HOBBITLIBOBJS)
	ranlib libhobbit.a || echo ""

libhobbit.so: $(HOBBITLIBOBJS)
	$(CC) $(CFLAGS) -shared -Wl,-soname,$(@).$(HOBBITLIBSOVERSION) -o $(@).$(HOBBITLIBSOVERSION).0 $(HOBBITLIBOBJS) $(PCRELIBS) $(NETLIBS) $(HOBBITZLIB)
	ln -sf ${@}.$(HOBBITLIBSOVERSION).0 $(@).$(HOBBITLIBSOVERSION)
	ln -sf $(@).$(HOBBITLIBSOVERSION) libhobbit.so

hobbitclient.a: $(CLIENTLIBOBJS)
	ar cr hobbitclient.a $(CLIENTLIBOBJS)
	ranlib hobbitclient.a || echo ""

compression.o: compression.c
	$(CC) $(CFLAGS) $(ZLIBINCDIR) -DHOBBITZLIB="$(HOBBITZLIB)" -c -o $@ $<

loadhosts.o: loadhosts.c loadhosts_file.c
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
	$(CC) $(CFLAGS) -DBBTOPDIR=\"$(BBTOPDIR)\" -DBBLOGDIR=\"$(BBLOGDIR)\" -DBBHOSTNAME=\"$(BBHOSTNAME)\" -DBBHOSTIP=\"$(BBHOSTIP)\" -DBBHOSTOS=\"$(BBHOSTOS)\" -DBUILD_HOME=\"$(BBTOPDIR)/server\" -c -o $@ environ.c

environ-client.o: environ.c
	$(CC) $(CFLAGS) -DBBTOPDIR=\"$(BBTOPDIR)\" -DBBLOGDIR=\"$(BBLOGDIR)\" -DBBHOSTNAME=\"$(BBHOSTNAME)\" -DBBHOSTIP=\"$(BBHOSTIP)\" -DBBHOSTOS=\"$(BBHOSTOS)\" -DBUILD_HOME=\"$(BBTOPDIR)/client\" -c -o $@ environ.c

color-client.o: color.c
	$(CC) $(CFLAGS) -DCLIENTONLY -c -o $@ $<

timefunc-client.o: timefunc.c
	$(CC) $(CFLAGS) -DCLIENTONLY -c -o $@ $<

loadhosts: loadhosts.c $(HOBBITLIBRARY)
	$(CC) $(CFLAGS) -DSTANDALONE -o $@ loadhosts.c $(HOBBITLIBRARY)

stackio: stackio.c $(HOBBITLIBRARY)
	$(CC) $(CFLAGS) -DSTANDALONE -o $@ stackio.c $(HOBBITLIBRARY)

availability: availability.c $(HOBBITLIBRARY)
	$(CC) $(CFLAGS) -DSTANDALONE -o $@ availability.c $(HOBBITLIBRARY)

md5: md5.c
	$(CC) $(CFLAGS) -DSTANDALONE `./test-endianness` -o $@ md5.c

sha1: sha1.c
	$(CC) $(CFLAGS) -DSTANDALONE `./test-endianness` -o $@ sha1.c

rmd160: rmd160c.c
	$(CC) $(CFLAGS) -DSTANDALONE `./test-endianness` -o $@ rmd160c.c

locator: locator.c $(HOBBITLIBRARY)
	$(CC) $(CFLAGS) -DSTANDALONE -o $@ locator.c $(HOBBITLIBRARY) $(NETLIBS)

msort: msort.c $(HOBBITLIBRARY)
	$(CC) $(CFLAGS) -DSTANDALONE -o $@ msort.c $(HOBBITLIBRARY) $(NETLIBS)

holidays: holidays.c $(HOBBITLIBRARY)
	$(CC) $(CFLAGS) -DSTANDALONE -o $@ holidays.c $(HOBBITLIBRARY) $(NETLIBS)

clean:
	rm -f *.o *.a libhobbit.so.* libhobbit.so *~ loadhosts stackio availability test-endianness md5 sha1 rmd160 locator msort holidays

install:
ifeq ($(HOBBITLIBRARY),libhobbit.so)
	cp -fp $(HOBBITLIBRARY) $(INSTALLROOT)$(INSTALLBINDIR)/
	/sbin/ldconfig -n $(INSTALLROOT)$(INSTALLBINDIR)
endif

