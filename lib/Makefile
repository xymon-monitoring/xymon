# bbgen library Makefile
#

BBGENLIBOBJS = calc.o cgi.o color.o digest.o encoding.o environ.o errormsg.o files.o headfoot.o hobbitrrd.o htmllog.o loadhosts.o links.o md5.o memory.o misc.o netservices.o rbtr.o sendmsg.o sig.o stackio.o timefunc.o timing.o url.o

CLIENTLIBOBJS = environ.o errormsg.o memory.o misc.o sendmsg.o sig.o stackio.o timefunc.o

CFLAGS += -I. -I../include 

all: libbbgen.a loadhosts stackio

client: hobbitclient.a

libbbgen.a: $(BBGENLIBOBJS)
	ar cr libbbgen.a $(BBGENLIBOBJS)
	ranlib libbbgen.a || echo ""

hobbitclient.a: $(CLIENTLIBOBJS)
	ar cr hobbitclient.a $(CLIENTLIBOBJS)
	ranlib hobbitclient.a || echo ""

loadhosts.o: loadhosts.c loadhosts_file.c
	$(CC) $(CFLAGS) -c -o $@ loadhosts.c

digest.o: digest.c
	$(CC) $(CFLAGS) $(SSLFLAGS) $(SSLINCDIR) -c -o $@ digest.c

headfoot.o: headfoot.c
	$(CC) $(CFLAGS) $(PCREINCDIR) -c -o $@ headfoot.c

environ.o: environ.c
	$(CC) $(CFLAGS) -DBBTOPDIR=\"$(BBTOPDIR)\" -DBBLOGDIR=\"$(BBLOGDIR)\" -DBBHOSTNAME=\"$(BBHOSTNAME)\" -DBBHOSTIP=\"$(BBHOSTIP)\" -DBBHOSTOS=\"$(BBHOSTOS)\" -c -o $@ environ.c

loadhosts: loadhosts.c libbbgen.a
	$(CC) $(CFLAGS) -DSTANDALONE -o $@ loadhosts.c ./libbbgen.a

stackio: stackio.c libbbgen.a
	$(CC) $(CFLAGS) -DSTANDALONE -o $@ stackio.c ./libbbgen.a

clean:
	rm -f *.o *.a *~ loadhosts stackio

