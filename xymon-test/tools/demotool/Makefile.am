## Copyright (C) 2006 Free Software Foundation, Inc.
## This Makefile.am is free software; the Free Software Foundation
## gives unlimited permission to copy, distribute and modify it.
#

AM_CPPFLAGS = -I$(srcdir)/../lib -I${srcdir}/../include
bin_PROGRAMS = demotool 
demotool_SOURCES = demotool.c
demotool_CPPFLAGS = -I$(srcdir)/../../lib


# We need to addd libsocket when in OpenSoalris 
if ON_OPENSOLARIS
demotool_LDADD = ../../lib/libxymon.la -lz 
else
demotool_LDADD = ../../lib/libxymon.la -lrt -lz 
demotool_LDADD += -lsocket -lnsl
endif