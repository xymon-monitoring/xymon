# Directory with Xymon source-code. You must have run "make lib-build" before building this
XYMONDIR = $(HOME)/xymon/4.3.0

# Directory for binaries
BINDIR = /usr/local/bin
# web template directory
WEBDIR = /etc/hobbit/web
# CGI script directory
CGIDIR = /usr/lib/hobbit/cgi-secure

PROGRAMS = smsadmin.cgi smsqueue
WEBFILES = sms_footer sms_form sms_header sms_reciplist_form sms_summary_header
CGIFILES = smsadmin.sh

LIBOBJS = $(XYMONDIR)/lib/libbbgen.a
CC = gcc
CFLAGS = -g -Wall -Wno-unused -D_REENTRANT -D_LARGEFILE_SOURCE -D_FILE_OFFSET_BITS=64 -DLINUX -Wno-pointer-sign -I$(XYMONDIR)/include

all: $(PROGRAMS)

smsadmin.cgi: smsadmin.o $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ smsadmin.o $(LIBOBJS) -lpcre -lrt

smsqueue: smsqueue.o $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ smsqueue.o $(LIBOBJS) -lpcre -lrt

install:
	cp $(PROGRAMS) $(BINDIR)/
	cp $(WEBFILES) $(WEBDIR)/
	cp $(CGIFILES) $(CGIDIR)/

clean:
	rm -f *.o *~ $(PROGRAMS)

