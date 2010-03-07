## Copyright (C) 2006 Free Software Foundation, Inc.
## This Makefile.am is free software; the Free Software Foundation
## gives unlimited permission to copy, distribute and modify it.
#
man_MANS = bbcombotest.1 bbcombotest.cfg.5 client-local.cfg.5 hobbit-alerts.cfg.5 \
hobbit-clients.cfg.5 hobbitd.8 hobbitd_alert.8 hobbitd_channel.8 hobbitd_client.8 \
hobbitd_filestore.8 hobbitd_history.8 hobbitd_hostdata.8 hobbitd_rrd.8 hobbitd_sample.8 \
hobbitfetch.8 hobbit-mailack.8 hobbitweb.5 trimhistory.8
bin_SCRIPTS = hobbit.sh


AM_CPPFLAGS = -I$(srcdir)/../lib  -I$(srcdir)/../include  -I/usr/include/pcre
#bin_PROGRAMS = 
bin_PROGRAMS = hobbitd hobbitd_channel hobbitd_locator hobbitd_filestore  hobbitd_history \
hobbitd_alert hobbitd_rrd hobbitd_sample hobbitd_client hobbitd_hostdata hobbitd_capture \
hobbitfetch hobbit-mailack trimhistory bbcombotest convertnk rrdcachectl

hobbitd_SOURCES = hobbitd.c hobbitd_buffer.c hobbitd_ipc.c
hobbitd_LDADD = ../lib/libxymon.la -lrt -lz -lsocket -lnsl

# converted from CHANNELOBJS   = hobbitd_channel.o hobbitd_buffer.o hobbitd_ipc.o
hobbitd_channel_SOURCES = hobbitd_channel.c hobbitd_buffer.c hobbitd_ipc.c
hobbitd_channel_LDADD = ../lib/libxymon.la

hobbitd_locator_SOURCES = hobbitd_locator.c
hobbitd_locator_LDADD = ../lib/libxymon.la


#FILESTOREOBJS = hobbitd_filestore.o hobbitd_worker.o hobbitd_buffer.o
hobbitd_filestore_SOURCES = hobbitd_filestore.c hobbitd_worker.c hobbitd_buffer.c
hobbitd_filestore_LDADD = ../lib/libxymon.la

#HISTORYOBJS   = hobbitd_history.o   hobbitd_worker.o hobbitd_buffer.o
hobbitd_history_SOURCES = hobbitd_history.c hobbitd_worker.c hobbitd_buffer.c
hobbitd_history_LDADD = ../lib/libxymon.la


# ALERTOBJS     = hobbitd_alert.o     hobbitd_worker.o hobbitd_buffer.o do_alert.o
hobbitd_alert_SOURCES = hobbitd_alert.c hobbitd_worker.c hobbitd_buffer.c do_alert.c
hobbitd_alert_LDADD = ../lib/libxymon.la


# RRDOBJS       = hobbitd_rrd.o       hobbitd_worker.o hobbitd_buffer.o do_rrd.o
hobbitd_rrd_SOURCES = hobbitd_rrd.c hobbitd_worker.c hobbitd_buffer.c do_rrd.c
hobbitd_rrd_LDADD = ../lib/libxymon.la


# SAMPLEOBJS    = hobbitd_sample.o    hobbitd_worker.o hobbitd_buffer.o
hobbitd_sample_SOURCES = hobbitd_sample.c hobbitd_worker.c hobbitd_buffer.c
hobbitd_sample_LDADD = ../lib/libxymon.la

hobbitd_client_SOURCES = hobbitd_client.c  hobbitd_worker.c hobbitd_buffer.c client_config.c
hobbitd_client_LDADD = ../lib/libxymon.la

hobbitd_hostdata_SOURCES  = hobbitd_hostdata.c  hobbitd_worker.c hobbitd_buffer.c
hobbitd_hostdata_LDADD = ../lib/libxymon.la

hobbitd_capture_SOURCES  = hobbitd_capture.c   hobbitd_worker.c hobbitd_buffer.c
hobbitd_capture_LDADD = ../lib/libxymon.la


hobbitfetch_SOURCES  = hobbitfetch.c  
hobbitfetch_LDADD = ../lib/libxymon.la


hobbit_mailack_SOURCES  = hobbit-mailack.c  
hobbit_mailack_LDADD = ../lib/libxymon.la

trimhistory_SOURCES  = trimhistory.c  
trimhistory_LDADD = ../lib/libxymon.la


bbcombotest_SOURCES  = bbcombotest.c  
bbcombotest_LDADD = ../lib/libxymon.la


convertnk_SOURCES  = convertnk.c  
convertnk_LDADD = ../lib/libxymon.la

rrdcachectl_SOURCES  = rrdcachectl.c  
rrdcachectl_LDADD = ../lib/libxymon.la
