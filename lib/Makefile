# bbgen library Makefile
#

BBGENLIBOBJS = bblarrd.o calc.o color.o digest.o encoding.o errormsg.o headfoot.o htmllog.o misc.o sendmsg.o sig.o stackio.o timefunc.o timing.o url.o

CFLAGS += -I. -I../include 

all: libbbgen.a

libbbgen.a: $(BBGENLIBOBJS)
	ar cr libbbgen.a $(BBGENLIBOBJS)
	ranlib libbbgen.a || echo ""

digest.o: digest.c
	$(CC) $(CFLAGS) $(SSLFLAGS) $(SSLINCDIR) -c -o $@ digest.c

clean:
	rm -f *.o *.a *~

