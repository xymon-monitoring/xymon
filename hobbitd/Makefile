CC = gcc
CFLAGS = -g -I.. -DMAXMSG=8192 -Wall

LIBOBJS = ../debug.o ../util.o
BBDOBJS = bbgend.o bbdutil.o bbdworker.o loadhosts.o
CHANNELOBJS = bbd_channel.o bbdutil.o

SAMPLEOBJS = bbd_sample.o bbdworker.o
FILESTOREOBJS = bbd_filestore.o bbdworker.o
HISTORYOBJS = bbd_history.o bbdworker.o
ALERTOBJS = bbd_alert.o bbdworker.o do_alert.o

all: bbgend bbd_channel bbd_filestore bbd_history bbd_alert

bbgend: $(BBDOBJS) $(LIBOBJS)
	$(CC) -o $@ $(BBDOBJS) $(LIBOBJS)

bbd_channel: $(CHANNELOBJS) $(LIBOBJS)
	$(CC) -o $@ $(CHANNELOBJS) $(LIBOBJS)

bbd_sample: $(SAMPLEOBJS) $(LIBOBJS)
	$(CC) -o $@ $(SAMPLEOBJS) $(LIBOBJS)

bbd_filestore: $(FILESTOREOBJS) $(LIBOBJS)
	$(CC) -o $@ $(FILESTOREOBJS) $(LIBOBJS)

bbd_history: $(HISTORYOBJS) $(LIBOBJS)
	$(CC) -o $@ $(HISTORYOBJS) $(LIBOBJS)

bbd_alert: $(ALERTOBJS) $(LIBOBJS)
	$(CC) -o $@ $(ALERTOBJS) $(LIBOBJS) -lpcre

%.o: %.c bbgend.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f bbgend bbd_channel bbd_sample bbd_filestore bbd_history bbd_alert *.o *~

