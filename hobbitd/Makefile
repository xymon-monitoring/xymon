CC = gcc
CFLAGS = -g -I.. -DMAXMSG=8192 -Wall

LIBOBJS = ../debug.o ../util.o
BBDOBJS = bbd_net.o bbdutil.o loadhosts.o
CHANNELOBJS = bbd_channel.o bbdutil.o

all: bbd_net bbd_channel

bbd_net: $(BBDOBJS)
	$(CC) -o $@ $(BBDOBJS) $(LIBOBJS)

bbd_channel: $(CHANNELOBJS)
	$(CC) -o $@ $(CHANNELOBJS) $(LIBOBJS)

%.o: %.c bbd_net.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f bbd_net bbd_channel *.o *~

