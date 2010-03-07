## Copyright (C) 2006 Free Software Foundation, Inc.
## This Makefile.am is free software; the Free Software Foundation
## gives unlimited permission to copy, distribute and modify it.
#

AM_CPPFLAGS = -I$(srcdir)/../lib -I$(srcdir)/../include -I/usr/include/pcre
#bin_PROGRAMS = hobbitlaunch logfetch clientupdate orcahobbit msgcache
bin_PROGRAMS = logfetch clientupdate orcahobbit msgcache

#hobbitlaunch_SOURCES = common/hobbitlaunch.c 
#hobbitlaunch_LDADD = ../lib/libxymonclient.la

logfetch_SOURCES = logfetch.c 
logfetch_LDADD = ../lib/libxymonclient.la

clientupdate_SOURCES = clientupdate.c 
clientupdate_LDADD = ../lib/libxymonclient.la

orcahobbit_SOURCES = orcahobbit.c 
orcahobbit_LDADD = ../lib/libxymonclient.la

msgcache_SOURCES = msgcache.c
msgcache_LDADD = -lrt -lz ../lib/libxymonclient.la  

# # We need to addd libsocket when in OpenSoalris 
# if ON_OPENSOLARIS
# msgcache_LDADD += -lsocket -lnsl
# endif


