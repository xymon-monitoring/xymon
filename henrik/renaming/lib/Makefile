# Xymon library Makefile
#

XYMONLIBOBJS = osdefs.o acklog.o availability.o calc.o cgi.o cgiurls.o clientlocal.o color.o crondate.o digest.o encoding.o environ.o errormsg.o eventlog.o files.o headfoot.o hobbitrrd.o holidays.o htmllog.o ipaccess.o loadalerts.o loadhosts.o loadnkconf.o locator.o links.o matching.o md5.o memory.o misc.o msort.o netservices.o notifylog.o rbtr.o readmib.o reportlog.o rmd160c.o sendmsg.o sha1.o sha2.o sig.o stackio.o strfunc.o suid.o timefunc.o timing.o url.o

CLIENTLIBOBJS = osdefs.o cgiurls.o color-client.o crondate.o digest.o encoding.o environ-client.o errormsg.o holidays.o ipaccess.o loadhosts.o md5.o memory.o misc.o msort.o rbtr.o rmd160c.o sendmsg.o sha1.o sha2.o sig.o stackio.o strfunc.o suid.o timefunc-client.o
ifeq ($(LOCALCLIENT),yes)
	CLIENTLIBOBJS += matching.o
endif


CFLAGS += -I. -I../include 

all: test-endianness libxymon.a xymonclient.a loadhosts stackio availability md5 sha1 rmd160 locator

client: test-endianness xymonclient.a

test-endianness: test-endianness.c
	$(CC) $(CFLAGS) -o $@ $<

libxymon.a: $(XYMONLIBOBJS)
	ar cr libxymon.a $(XYMONLIBOBJS)
	ranlib libxymon.a || echo ""

xymonclient.a: $(CLIENTLIBOBJS)
	ar cr xymonclient.a $(CLIENTLIBOBJS)
	ranlib xymonclient.a || echo ""

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

loadhosts: loadhosts.c libxymon.a
	$(CC) $(CFLAGS) -DSTANDALONE -o $@ loadhosts.c ./libxymon.a $(LIBRTDEF)

stackio: stackio.c libxymon.a
	$(CC) $(CFLAGS) -DSTANDALONE -o $@ stackio.c ./libxymon.a $(LIBRTDEF)

availability: availability.c libxymon.a
	$(CC) $(CFLAGS) -DSTANDALONE -o $@ availability.c ./libxymon.a $(LIBRTDEF)

md5: md5.c
	$(CC) $(CFLAGS) -DSTANDALONE `./test-endianness` -o $@ md5.c

sha1: sha1.c
	$(CC) $(CFLAGS) -DSTANDALONE `./test-endianness` -o $@ sha1.c

rmd160: rmd160c.c
	$(CC) $(CFLAGS) -DSTANDALONE `./test-endianness` -o $@ rmd160c.c

locator: locator.c
	$(CC) $(CFLAGS) -DSTANDALONE -o $@ locator.c ./libxymon.a $(NETLIBS) $(LIBRTDEF)

clean:
	rm -f *.o *.a *~ loadhosts stackio availability test-endianness md5 sha1 rmd160 locator

