# bbgen library Makefile
#

BBGENLIBOBJS = osdefs.o acklog.o availability.o calc.o cgi.o cgiurls.o color.o digest.o encoding.o environ.o errormsg.o eventlog.o files.o headfoot.o hobbitrrd.o htmllog.o loadalerts.o loadhosts.o loadnkconf.o links.o matching.o md5.o memory.o misc.o netservices.o rbtr.o reportlog.o sendmsg.o sha1.o sig.o stackio.o strfunc.o timefunc.o timing.o url.o

CLIENTLIBOBJS = osdefs.o color.o digest.o encoding.o environ-client.o errormsg.o loadhosts.o md5.o memory.o misc.o rbtr.o sendmsg.o sha1.o sig.o stackio.o strfunc.o timefunc.o
ifeq ($(LOCALCLIENT),yes)
	CLIENTLIBOBJS += matching.o
endif


CFLAGS += -I. -I../include 

all: test-endianness libbbgen.a hobbitclient.a loadhosts stackio availability sha1s

client: test-endianness hobbitclient.a sha1c

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

environ.o: environ.c
	$(CC) $(CFLAGS) -DBBTOPDIR=\"$(BBTOPDIR)\" -DBBLOGDIR=\"$(BBLOGDIR)\" -DBBHOSTNAME=\"$(BBHOSTNAME)\" -DBBHOSTIP=\"$(BBHOSTIP)\" -DBBHOSTOS=\"$(BBHOSTOS)\" -DBUILD_HOME=\"$(BBTOPDIR)/server\" -c -o $@ environ.c

environ-client.o: environ.c
	$(CC) $(CFLAGS) -DBBTOPDIR=\"$(BBTOPDIR)\" -DBBLOGDIR=\"$(BBLOGDIR)\" -DBBHOSTNAME=\"$(BBHOSTNAME)\" -DBBHOSTIP=\"$(BBHOSTIP)\" -DBBHOSTOS=\"$(BBHOSTOS)\" -DBUILD_HOME=\"$(BBTOPDIR)/client\" -c -o $@ environ.c

loadhosts: loadhosts.c libbbgen.a
	$(CC) $(CFLAGS) -DSTANDALONE -o $@ loadhosts.c ./libbbgen.a

stackio: stackio.c libbbgen.a
	$(CC) $(CFLAGS) -DSTANDALONE -o $@ stackio.c ./libbbgen.a

availability: availability.c libbbgen.a
	$(CC) $(CFLAGS) -DSTANDALONE -o $@ availability.c ./libbbgen.a

sha1s: sha1.c libbbgen.a
	$(CC) $(CFLAGS) -DSTANDALONE `./test-endianness` -o $@ sha1.c ./libbbgen.a

sha1c: sha1.c hobbitclient.a
	$(CC) $(CFLAGS) -DSTANDALONE `./test-endianness` -o $@ sha1.c ./hobbitclient.a

clean:
	rm -f *.o *.a *~ loadhosts stackio availability test-endianness sha1c sha1s

