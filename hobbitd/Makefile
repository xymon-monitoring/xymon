CC = gcc
CFLAGS = -g -I.. -DMAXMSG=8192 -Wall

LIBOBJS = ../debug.o ../util.o
BBDOBJS = bbd_net.o bbdutil.o loadhosts.o
CHANNELOBJS = bbd_channel.o bbdutil.o

FILESTOREOBJS = bbd_filestore.o bbdworker.o
HISTORYOBJS = bbd_history.o bbdworker.o

all: bbd_net bbd_channel bbd_filestore bbd_history

bbd_net: $(BBDOBJS)
	$(CC) -o $@ $(BBDOBJS) $(LIBOBJS)

bbd_channel: $(CHANNELOBJS) $(LIBOBJS)
	$(CC) -o $@ $(CHANNELOBJS) $(LIBOBJS)

bbd_filestore: $(FILESTOREOBJS) $(LIBOBJS)
	$(CC) -o $@ $(FILESTOREOBJS) $(LIBOBJS)

bbd_history: $(HISTORYOBJS) $(LIBOBJS)
	$(CC) -o $@ $(HISTORYOBJS) $(LIBOBJS)

%.o: %.c bbd_net.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f bbd_net bbd_channel bbd_filestore bbd_history *.o *~

