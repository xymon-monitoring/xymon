## Copyright (C) 2006 Free Software Foundation, Inc.
## This Makefile.am is free software; the Free Software Foundation
## gives unlimited permission to copy, distribute and modify it.
#

AM_CPPFLAGS = -I$(srcdir)/../lib  -I$(srcdir)/../include  -I/usr/include/pcre
bin_PROGRAMS = bbproxy bbmessage.cgi

bbproxy_SOURCES = bbproxy.c 
bbproxy_LDADD = ../lib/libxymon.la


bbmessage_cgi_SOURCES = bbmessage.c


if ON_OPENSOLARIS
bbmessage_cgi_LDADD = ../lib/libxymon.la  -lz 
else 
bbmessage_cgi_LDADD = ../lib/libxymon.la -lrt -lz 
bbproxy_LDADD += -lsocket -lnsl
bbmessage_cgi_LDADD +=  -lsocket -lnsl
endif
