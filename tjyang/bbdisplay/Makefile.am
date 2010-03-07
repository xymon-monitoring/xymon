## Copyright (C) 2006 Free Software Foundation, Inc.
## This Makefile.am is free software; the Free Software Foundation
## gives unlimited permission to copy, distribute and modify it.
#

AM_CPPFLAGS = -I$(srcdir)/../lib  -I$(srcdir)/../include  -I/usr/include/pcre
bin_PROGRAMS = bbgen 

bbgen_SOURCES = bbgen.c loadbbhosts.c loaddata.c bbconvert.c pagegen.c process.c wmlgen.c rssgen.c util.c debug.c csvreport.c
bbgen_LDADD = ../lib/libxymon.la -lrt -lz -lsocket -lnsl
