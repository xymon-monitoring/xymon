# bbgen library Makefile
#

BBGENLIBOBJS = calc.o color.o encoding.o errormsg.o headfoot.o misc.o sendmsg.o sig.o stackio.o timefunc.o timing.o url.o

CFLAGS += -I. -I../include 

all: libbbgen.a

libbbgen.a: $(BBGENLIBOBJS)
	ar cr libbbgen.a $(BBGENLIBOBJS)
	ranlib libbbgen.a || echo ""

clean:
	rm -f *.o *.a *~

