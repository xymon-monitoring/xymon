# bbgen library Makefile
#

BBGENLIBOBJS = osdefs.o acklog.o availability.o calc.o cgi.o cgiurls.o clientlocal.o color.o digest.o encoding.o environ.o errormsg.o eventlog.o files.o headfoot.o hobbitrrd.o htmllog.o ipaccess.o loadalerts.o loadhosts.o loadnkconf.o locator.o links.o matching.o md5.o memory.o misc.o netservices.o rbtr.o reportlog.o rmd160c.o sendmsg.o sha1.o sig.o stackio.o strfunc.o suid.o timefunc.o timing.o url.o

CLIENTLIBOBJS = osdefs.o cgiurls.o color-client.o digest.o encoding.o environ-client.o errormsg.o ipaccess.o loadhosts.o md5.o memory.o misc.o rbtr.o rmd160c.o sendmsg.o sha1.o sig.o stackio.o strfunc.o suid.o timefunc-client.o
ifeq ($(LOCALCLIENT),yes)
	CLIENTLIBOBJS += matching.o
endif


CFLAGS += -I. -I../include 

all: test-endianness libbbgen.a hobbitclient.a loadhosts stackio availability md5 sha1 rmd160 locator

client: test-endianness hobbitclient.a

test-endianness: test-endianness.c
	$(CC) $(CFLAGS) -o $@ $<

libbbgen.a: $(BBGENLIBOBJS)
	ar cr libbbgen.a $(BBGENLIBOBJS)
	ranlib libbbgen.a || echo ""

hobbitclient.a: $(CLIENTLIBOBJS)
	ar cr hobbitclient.a $(CLIENTLIBOBJS)
	ranlib hobbitclient.a || echo ""

loadhosts.o: loadhosts.c loadhosts_file.c
	$(CC) $(CFLAGS) -c -o $@ loadhosts.c

eventlog.o: eventlog.c
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

loadhosts: loadhosts.c libbbgen.a
	$(CC) $(CFLAGS) -DSTANDALONE -o $@ loadhosts.c ./libbbgen.a

stackio: stackio.c libbbgen.a
	$(CC) $(CFLAGS) -DSTANDALONE -o $@ stackio.c ./libbbgen.a

availability: availability.c libbbgen.a
	$(CC) $(CFLAGS) -DSTANDALONE -o $@ availability.c ./libbbgen.a

md5: md5.c
	$(CC) $(CFLAGS) -DSTANDALONE `./test-endianness` -o $@ md5.c

sha1: sha1.c
	$(CC) $(CFLAGS) -DSTANDALONE `./test-endianness` -o $@ sha1.c

rmd160: rmd160c.c
	$(CC) $(CFLAGS) -DSTANDALONE `./test-endianness` -o $@ rmd160c.c

locator: locator.c libbbgen.a
	$(CC) $(CFLAGS) -DSTANDALONE -o $@ locator.c ./libbbgen.a $(NETLIBS)

clean:
	rm -f *.o *.a *~ loadhosts stackio availability test-endianness md5 sha1 rmd160 locator

