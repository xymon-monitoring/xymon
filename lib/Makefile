# bbgen library Makefile
#

BBGENLIBOBJS = bbalert.o bblarrd.o calc.o color.o digest.o encoding.o errormsg.o files.o headfoot.o htmllog.o loadhosts.o links.o misc.o rbtr.o savelog.o sendmsg.o sig.o stackio.o timefunc.o timing.o url.o

CFLAGS += -I. -I../include 

all: libbbgen.a loadhosts

libbbgen.a: $(BBGENLIBOBJS)
	ar cr libbbgen.a $(BBGENLIBOBJS)
	ranlib libbbgen.a || echo ""

digest.o: digest.c
	$(CC) $(CFLAGS) $(SSLFLAGS) $(SSLINCDIR) -c -o $@ digest.c

loadhosts: loadhosts.c libbbgen.a
	$(CC) $(CFLAGS) -DSTANDALONE -o $@ loadhosts.c ./libbbgen.a

clean:
	rm -f *.o *.a *~

