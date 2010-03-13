## Copyright (C) 2006 Free Software Foundation, Inc.
## This Makefile.am is free software; the Free Software Foundation
## gives unlimited permission to copy, distribute and modify it.
#

AM_CPPFLAGS = -I$(srcdir)/../lib -I${srcdir}/../include
bin_PROGRAMS = tr2latex 
tr2latex_SOURCES = tr2latex.c tr.c subs.c
include_HEADERS = protos.h setups.h simil.h greek.h macros.h maths.h flip.h forbid.h

## We need to addd libsocket when in OpenSoalris 
#if ON_OPENSOLARIS
#tr2latex_LDADD = ../lib/libxymon.la -lz 
#else
#tr2latex_LDADD = ../lib/libxymon.la -lrt -lz 
#tr2latex_LDADD += -lsocket -lnsl
#endif