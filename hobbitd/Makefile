PROGRAMS = bbgend bbgend_channel bbgend_filestore bbgend_history bbgend_alert bbgend_larrd bbgend_sample

LIBOBJS = ../lib/libbbgen.a

BBGENDOBJS = bbgend.o bbgend_ipc.o bbgend_worker.o loadhosts.o
CHANNELOBJS = bbgend_channel.o bbgend_ipc.o

SAMPLEOBJS    = bbgend_sample.o    bbgend_worker.o
FILESTOREOBJS = bbgend_filestore.o bbgend_worker.o
HISTORYOBJS   = bbgend_history.o   bbgend_worker.o
ALERTOBJS     = bbgend_alert.o     bbgend_worker.o do_alert.o
LARRDOBJS     = bbgend_larrd.o     bbgend_worker.o do_larrd.o
SAMPLEOBJS    = bbgend_sample.o    bbgend_worker.o

all: $(PROGRAMS)

bbgend: $(BBGENDOBJS) $(LIBOBJS)
	$(CC) -o $@ $(BBGENDOBJS) $(LIBOBJS)

bbgend_channel: $(CHANNELOBJS) $(LIBOBJS)
	$(CC) -o $@ $(CHANNELOBJS) $(LIBOBJS)

bbgend_filestore: $(FILESTOREOBJS) $(LIBOBJS)
	$(CC) -o $@ $(FILESTOREOBJS) $(LIBOBJS)

bbgend_history: $(HISTORYOBJS) $(LIBOBJS)
	$(CC) -o $@ $(HISTORYOBJS) $(LIBOBJS)

bbgend_alert: $(ALERTOBJS) $(LIBOBJS)
	$(CC) -o $@ $(ALERTOBJS) $(LIBOBJS) -lpcre

bbgend_larrd: $(LARRDOBJS) $(LIBOBJS)
	$(CC) -o $@ $(LARRDOBJS) $(LIBOBJS) -lrrd

do_larrd.o: do_larrd.c do_larrd.h larrd/*.c
	$(CC) $(CFLAGS) -c -o $@ do_larrd.c

bbgend_sample: $(SAMPLEOBJS) $(LIBOBJS)
	$(CC) -o $@ $(SAMPLEOBJS) $(LIBOBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(PROGRAMS) *.o *~ larrd/*~

install: install-bin install-man

install-bin:
	cp -p $(PROGRAMS) $(BBHOME)/bin/

install-man:
	mkdir -p $(MANROOT)/man8
	cp -p *.8 $(MANROOT)/man8/

