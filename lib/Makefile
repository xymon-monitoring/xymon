# bbgen library Makefile
#

BBGENLIBOBJS = bbalert.o bblarrd.o calc.o color.o digest.o encoding.o environ.o errormsg.o files.o headfoot.o htmllog.o loadhosts.o links.o md5.o memory.o misc.o netservices.o rbtr.o savelog.o sendmsg.o sig.o stackio.o timefunc.o timing.o url.o

CFLAGS += -I. -I../include 

all: libbbgen.a loadhosts

libbbgen.a: $(BBGENLIBOBJS)
	ar cr libbbgen.a $(BBGENLIBOBJS)
	ranlib libbbgen.a || echo ""

loadhosts.o: loadhosts.c loadhosts_file.c
	$(CC) $(CFLAGS) -c -o $@ loadhosts.c

digest.o: digest.c
	$(CC) $(CFLAGS) $(SSLFLAGS) $(SSLINCDIR) -c -o $@ digest.c

environ.o: environ.c
	$(CC) $(CFLAGS) -DBBTOPDIR=\"$(BBTOPDIR)\" -DBBLOGDIR=\"$(BBLOGDIR)\" -DBBHOSTNAME=\"$(BBHOSTNAME)\" -DBBHOSTIP=\"$(BBHOSTIP)\" -DBBHOSTOS=\"$(BBHOSTOS)\" -c -o $@ environ.c

loadhosts: loadhosts.c libbbgen.a
	$(CC) $(CFLAGS) -DSTANDALONE -o $@ loadhosts.c ./libbbgen.a

clean:
	rm -f *.o *.a *~ loadhosts

