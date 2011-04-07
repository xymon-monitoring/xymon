# Directory with Xymon source-code. You must have run "make lib-build" before building this
XYMONDIR = $(HOME)/xymon/trunk

# Directory for binaries
BINDIR = /usr/local/bin
# web template directory
WEBDIR = /etc/hobbit/web
# CGI script directory
CGIDIR = /usr/lib/hobbit/cgi-secure

PROGRAMS = webcert.cgi
WEBFILES = webcert_footer webcert_form webcert_header
CGIFILES = webcert.sh

LIBOBJS = $(XYMONDIR)/lib/libxymon.a
CC = gcc
CFLAGS = -g -Wall -Wno-unused -D_REENTRANT -D_LARGEFILE_SOURCE -D_FILE_OFFSET_BITS=64 -DLINUX -Wno-pointer-sign -I$(XYMONDIR)/include

all: $(PROGRAMS)

webcert.cgi: webcert.o $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ webcert.o $(LIBOBJS) -lpcre -lrt

install:
	cp $(PROGRAMS) $(BINDIR)/
	cp $(WEBFILES) $(WEBDIR)/
	cp $(CGIFILES) $(CGIDIR)/

clean:
	rm -f *.o *~ $(PROGRAMS)

