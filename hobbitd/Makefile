PROGRAMS = bbgend bbgend_channel bbgend_filestore bbgend_history bbgend_alert bbgend_larrd bbgend_sample startbb.sh
CGISCRIPTS = bb-ack.sh larrd-grapher.cgi maint.pl

LIBOBJS = ../lib/libbbgen.a

BBGENDOBJS = bbgend.o bbgend_ipc.o bbgend_worker.o
CHANNELOBJS = bbgend_channel.o bbgend_ipc.o

SAMPLEOBJS    = bbgend_sample.o    bbgend_worker.o
FILESTOREOBJS = bbgend_filestore.o bbgend_worker.o
HISTORYOBJS   = bbgend_history.o   bbgend_worker.o
ALERTOBJS     = bbgend_alert.o     bbgend_worker.o do_alert.o
LARRDOBJS     = bbgend_larrd.o     bbgend_worker.o do_larrd.o
SAMPLEOBJS    = bbgend_sample.o    bbgend_worker.o

all: $(PROGRAMS) $(CGISCRIPTS) cfgfiles

bbgend: $(BBGENDOBJS) $(LIBOBJS)
	$(CC) -o $@ $(BBGENDOBJS) $(LIBOBJS) $(NETLIBS)

bbgend_channel: $(CHANNELOBJS) $(LIBOBJS)
	$(CC) -o $@ $(CHANNELOBJS) $(LIBOBJS)

bbgend_filestore: $(FILESTOREOBJS) $(LIBOBJS)
	$(CC) -o $@ $(FILESTOREOBJS) $(LIBOBJS)

bbgend_history: $(HISTORYOBJS) $(LIBOBJS)
	$(CC) -o $@ $(HISTORYOBJS) $(LIBOBJS)

bbgend_alert: $(ALERTOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(ALERTOBJS) $(LIBOBJS) $(PCRELIBS)

bbgend_larrd: $(LARRDOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(LARRDOBJS) $(LIBOBJS) $(RRDLIBS)

do_alert.o: do_alert.c
	$(CC) $(CFLAGS) $(PCREINCDIR) -c -o $@ do_alert.c

do_larrd.o: do_larrd.c do_larrd.h larrd/*.c
	$(CC) $(CFLAGS) $(RRDINCDIR) -c -o $@ do_larrd.c

bbgend_sample: $(SAMPLEOBJS) $(LIBOBJS)
	$(CC) -o $@ $(SAMPLEOBJS) $(LIBOBJS)

startbb.sh: startbb.sh.DIST
	cat $< | sed -e 's!@BBHOME@!$(BBHOME)!g' | sed -e 's!@BBLOGDIR@!$(BBLOGDIR)!g' | sed -e 's!@BBUSER@!$(BBUSER)!g' >$@
	chmod 755 $@

bb-ack.sh: bb-ack.sh-bbgend.DIST
	cat $< | sed -e 's!@BBHOME@!$(BBHOME)!g' >$@
	chmod 755 $@

larrd-grapher.cgi: larrd-grapher.cgi.DIST
	cat $< | sed -e 's!@BBHOME@!$(BBHOME)!g' | sed -e 's!@BBVAR@!$(BBVAR)!g' >$@
	chmod 755 $@

maint.pl: maint.pl.DIST
	cat $< | sed -e 's!@BBHOME@!$(BBHOME)!g' | sed -e 's!@BBVAR@!$(BBVAR)!g' >$@
	chmod 755 $@

cfgfiles:
	cat etcfiles/bb-apache.conf.DIST | sed -e 's!@BBHOME@!$(BBHOME)!g' | sed -e 's!@CGIDIR@!$(CGIDIR)!g' | sed -e 's!@BBHOSTURL@!$(BBHOSTURL)!g' | sed -e 's!@BBCGIURL@!$(BBCGIURL)!g' >etcfiles/bb-apache.conf
	cat etcfiles/bbserver.env.DIST | sed -e 's!@BBTOPDIR@!$(BBTOPDIR)!g'| sed -e 's!@BBLOGDIR@!$(BBLOGDIR)!g'| sed -e 's!@BBHOSTNAME@!$(BBHOSTNAME)!g'| sed -e 's!@BBHOSTIP@!$(BBHOSTIP)!g'| sed -e 's!@BBHOSTOS@!$(BBHOSTOS)!g' | sed -e 's!@BBHOSTURL@!$(BBHOSTURL)!g' | sed -e 's!@BBCGIURL@!$(BBCGIURL)!g' >etcfiles/bbserver.env
	cat etcfiles/bb-hosts.DIST | sed -e 's!@BBHOSTNAME@!$(BBHOSTNAME)!g'| sed -e 's!@BBHOSTIP@!$(BBHOSTIP)!g' >etcfiles/bb-hosts
	cat etcfiles/bb-alerts.cfg.DIST >etcfiles/bb-alerts.cfg
	cat etcfiles/bbtasks.cfg.DIST >etcfiles/bbtasks.cfg
	cat wwwfiles/menu/menu_items.js.DIST | sed -e 's!@BBHOSTURL@!$(BBHOSTURL)!g' | sed -e 's!@BBCGIURL@!$(BBCGIURL)!g' >wwwfiles/menu/menu_items.js

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(PROGRAMS) $(CGISCRIPTS) *.o *~ larrd/*~ 
	rm -f etcfiles/bb-apache.conf etcfiles/bbserver.env etcfiles/bb-hosts etcfiles/bb-alerts.cfg etcfiles/bbtasks.cfg
	rm -f wwwfiles/menu/menu_items.js
	(find etcfiles/ -name "*~"; find wwwfiles/ -name "*~"; find webfiles/ -name "*~") | xargs rm -f

install: install-bin install-cgi install-man

install-bin:
	cp -fp $(PROGRAMS) $(BBHOME)/bin/

install-cgi:
	mkdir -p $(CGIDIR)
	cp -fp $(CGISCRIPTS) $(CGIDIR)/

install-man:
	mkdir -p $(MANROOT)/man8
	cp -fp *.8 $(MANROOT)/man8/

setup:
	@echo "*** WARNING ***"
	@echo "This installs a new bbgend server. Any modifications you"
	@echo "have done to existing files in ${BBHOME} may be lost."
	@echo "If you dont want this to happen, hit CTRL-C now!"
	@sleep 8
	@echo "OK, continuing installation..."
	cd wwwfiles; tar cf - . | (cd $(BBHOME)/www/; tar xf -)
	cd webfiles; tar cf - . | (cd $(BBHOME)/web/; tar xf -)
	cd etcfiles; ../../build/install-nooverwrite.sh etc/ bb-apache.conf bbserver.env bb-hosts bb-alerts.cfg bbtasks.cfg
	mkdir -p $(BBLOGDIR)
	chown $(BBUSER)         $(BBLOGDIR) $(BBHOME) $(BBHOME)/* $(BBHOME)/bin/* $(BBHOME)/etc/* $(BBHOME)/ext/* $(BBHOME)/web/* $(BBHOME)/www/gifs $(BBHOME)/www/gifs/* $(BBHOME)/www/menu $(BBHOME)/www/menu/* $(BBHOME)/www/help $(BBHOME)/www/help/* $(BBHOME)/www/notes $(BBHOME)/www/html $(BBHOME)/www/wml $(BBVAR) $(BBVAR)/* $(CGIDIR) $(CGIDIR)/*
	chgrp `id -g $(BBUSER)` $(BBLOGDIR) $(BBHOME) $(BBHOME)/* $(BBHOME)/bin/* $(BBHOME)/etc/* $(BBHOME)/ext/* $(BBHOME)/web/* $(BBHOME)/www/gifs $(BBHOME)/www/gifs/* $(BBHOME)/www/menu $(BBHOME)/www/menu/* $(BBHOME)/www/help $(BBHOME)/www/help/* $(BBHOME)/www/notes $(BBHOME)/www/html $(BBHOME)/www/wml $(BBVAR) $(BBVAR)/* $(CGIDIR) $(CGIDIR)/*

