PROGRAMS = hobbitd hobbitd_channel hobbitd_filter hobbitd_filestore hobbitd_history hobbitd_alert hobbitd_larrd hobbitd_sample starthobbit.sh hobbitgraph.cgi
CGISCRIPTS = bb-ack.sh hobbitgraph.sh maint.pl

LIBOBJS = ../lib/libbbgen.a

HOBBITDOBJS = hobbitd.o hobbitd_ipc.o
CHANNELOBJS = hobbitd_channel.o hobbitd_ipc.o
FILTEROBJS  = hobbitd_filter.o hobbitd_ipc.o

SAMPLEOBJS    = hobbitd_sample.o    hobbitd_worker.o
FILESTOREOBJS = hobbitd_filestore.o hobbitd_worker.o
HISTORYOBJS   = hobbitd_history.o   hobbitd_worker.o
ALERTOBJS     = hobbitd_alert.o     hobbitd_worker.o do_alert.o
LARRDOBJS     = hobbitd_larrd.o     hobbitd_worker.o do_larrd.o
SAMPLEOBJS    = hobbitd_sample.o    hobbitd_worker.o
HOBBITGRAPHOBJS= hobbitgraph.o

IDTOOL := $(shell if test `uname -s` = "SunOS"; then echo /usr/xpg4/bin/id; else echo id; fi)


all: $(PROGRAMS) $(CGISCRIPTS) cfgfiles moverrd.sh

# Need NETLIBS on Solaris for getservbyname(), called by parse_url()

hobbitd: $(HOBBITDOBJS) $(LIBOBJS)
	$(CC) -o $@ $(HOBBITDOBJS) $(LIBOBJS) $(NETLIBS)

hobbitd_channel: $(CHANNELOBJS) $(LIBOBJS)
	$(CC) -o $@ $(CHANNELOBJS) $(LIBOBJS)

hobbitd_filestore: $(FILESTOREOBJS) $(LIBOBJS)
	$(CC) -o $@ $(FILESTOREOBJS) $(LIBOBJS) $(NETLIBS)

hobbitd_history: $(HISTORYOBJS) $(LIBOBJS)
	$(CC) -o $@ $(HISTORYOBJS) $(LIBOBJS)

hobbitd_alert: $(ALERTOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(ALERTOBJS) $(LIBOBJS) $(PCRELIBS)

hobbitd_larrd: $(LARRDOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(LARRDOBJS) $(LIBOBJS) $(RRDLIBS) $(NETLIBS)

hobbitd_filter: $(FILTEROBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(FILTEROBJS) $(LIBOBJS)

do_alert.o: do_alert.c
	$(CC) $(CFLAGS) $(PCREINCDIR) -c -o $@ do_alert.c

do_larrd.o: do_larrd.c do_larrd.h larrd/*.c
	$(CC) $(CFLAGS) $(RRDINCDIR) -c -o $@ do_larrd.c

hobbitd_sample: $(SAMPLEOBJS) $(LIBOBJS)
	$(CC) -o $@ $(SAMPLEOBJS) $(LIBOBJS)

hobbitgraph.o: hobbitgraph.c
	$(CC) $(CFLAGS) $(PCREINCDIR) $(RRDINCDIR) -c -o $@ hobbitgraph.c

hobbitgraph.cgi: $(HOBBITGRAPHOBJS)
	$(CC) $(CFLAGS) -o $@ $(HOBBITGRAPHOBJS) $(LIBOBJS) $(PCRELIBS) $(RRDLIBS) $(NETLIBS)

starthobbit.sh: starthobbit.sh.DIST
	cat $< | sed -e 's!@BBHOME@!$(BBHOME)!g' | sed -e 's!@BBLOGDIR@!$(BBLOGDIR)!g' | sed -e 's!@BBUSER@!$(BBUSER)!g' | sed -e 's!@RUNTIMEDEFS@!$(RUNTIMEDEFS)!g' >$@
	chmod 755 $@

bb-ack.sh: bb-ack.sh.DIST
	cat $< | sed -e 's!@BBHOME@!$(BBHOME)!g' | sed -e 's!@RUNTIMEDEFS@!$(RUNTIMEDEFS)!g' >$@
	chmod 755 $@

hobbitgraph.sh: hobbitgraph.sh.DIST
	cat $< | sed -e 's!@BBHOME@!$(BBHOME)!g' | sed -e 's!@RUNTIMEDEFS@!$(RUNTIMEDEFS)!g' >$@
	chmod 755 $@

moverrd.sh: moverrd.sh.DIST
	cat $< | sed -e 's!@BBHOME@!$(BBHOME)!g' | sed -e 's!@BBVAR@!$(BBVAR)!g' >$@
	chmod 755 $@

maint.pl: maint.pl.DIST
	cat $< | sed -e 's!@BBHOME@!$(BBHOME)!g' | sed -e 's!@BBVAR@!$(BBVAR)!g' >$@
	chmod 755 $@

cfgfiles:
	cat etcfiles/hobbit-apache.conf.DIST | sed -e 's!@BBHOME@!$(BBHOME)!g' | sed -e 's!@CGIDIR@!$(CGIDIR)!g' | sed -e 's!@BBHOSTURL@!$(BBHOSTURL)!g' | sed -e 's!@BBCGIURL@!$(BBCGIURL)!g' >etcfiles/hobbit-apache.conf
	cat etcfiles/hobbitserver.cfg.DIST | sed -e 's!@BBTOPDIR@!$(BBTOPDIR)!g'| sed -e 's!@BBLOGDIR@!$(BBLOGDIR)!g'| sed -e 's!@BBHOSTNAME@!$(BBHOSTNAME)!g'| sed -e 's!@BBHOSTIP@!$(BBHOSTIP)!g'| sed -e 's!@BBHOSTOS@!$(BBHOSTOS)!g' | sed -e 's!@BBHOSTURL@!$(BBHOSTURL)!g' | sed -e 's!@BBCGIURL@!$(BBCGIURL)!g' | sed -e 's!@BBHOME@!$(BBHOME)!g' | sed -e 's!@BBVAR@!$(BBVAR)!g' | sed -e 's!@FPING@!$(FPING)!g' | sed -e 's!@RUNTIMEDEFS@!$(RUNTIMEDEFS)!g' >etcfiles/hobbitserver.cfg
	cat etcfiles/bb-hosts.DIST | sed -e 's!@BBHOSTNAME@!$(BBHOSTNAME)!g'| sed -e 's!@BBHOSTIP@!$(BBHOSTIP)!g' >etcfiles/bb-hosts
	cat etcfiles/hobbit-alerts.cfg.DIST >etcfiles/hobbit-alerts.cfg
	cat etcfiles/hobbitlaunch.cfg.DIST | sed -e 's!@BBHOME@!$(BBHOME)!g' >etcfiles/hobbitlaunch.cfg
	cat wwwfiles/menu/menu_items.js.DIST | sed -e 's!@BBHOSTURL@!$(BBHOSTURL)!g' | sed -e 's!@BBCGIURL@!$(BBCGIURL)!g' >wwwfiles/menu/menu_items.js

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(PROGRAMS) $(CGISCRIPTS) *.o *~ larrd/*~ moverrd.sh
	rm -f etcfiles/hobbit-apache.conf etcfiles/hobbitserver.cfg etcfiles/bb-hosts etcfiles/hobbit-alerts.cfg etcfiles/hobbitlaunch.cfg
	rm -f wwwfiles/menu/menu_items.js
	(find etcfiles/ -name "*~"; find wwwfiles/ -name "*~"; find webfiles/ -name "*~") | xargs rm -f

install: install-bin install-cgi install-man

install-bin:
	cp -fp $(PROGRAMS) $(BBHOME)/bin/

install-cgi:
	mkdir -p $(CGIDIR)
	cp -fp $(CGISCRIPTS) $(CGIDIR)/
	cd etcfiles; ../../build/install-nooverwrite.sh etc/ hobbitgraph.cfg columndoc.csv

install-man:
	mkdir -p $(MANROOT)/man1 $(MANROOT)/man5 $(MANROOT)/man8
	cp -fp *.1 $(MANROOT)/man1/
	cp -fp *.5 $(MANROOT)/man5/
	cp -fp *.8 $(MANROOT)/man8/

setup:
	@echo "*** WARNING ***"
	@echo "This installs a new Hobbit server. Any modifications you"
	@echo "have done to existing files in ${BBHOME} may be lost."
	@echo "If you dont want this to happen, hit CTRL-C now!"
	@sleep 8
	@echo "Continuing installation..."
	cd wwwfiles; tar cf - . | (cd $(BBHOME)/www/; tar xf -)
	cd webfiles; tar cf - . | (cd $(BBHOME)/web/; tar xf -)
	cd etcfiles; ../../build/install-nooverwrite.sh etc/ hobbit-apache.conf hobbitserver.cfg bb-hosts hobbit-alerts.cfg hobbitlaunch.cfg hobbitgraph.cfg
	mkdir -p $(BBLOGDIR)
	chown $(BBUSER)         $(BBLOGDIR) $(BBHOME) $(BBHOME)/* $(BBHOME)/bin/* $(BBHOME)/etc/* $(BBHOME)/ext/* $(BBHOME)/web/* $(BBHOME)/www/gifs $(BBHOME)/www/gifs/* $(BBHOME)/www/menu $(BBHOME)/www/menu/* $(BBHOME)/www/help $(BBHOME)/www/help/* $(BBHOME)/www/notes $(BBHOME)/www/html $(BBHOME)/www/wml $(BBVAR) $(BBVAR)/* $(CGIDIR) $(CGIDIR)/*
	chgrp `$(IDTOOL) -g $(BBUSER)` $(BBLOGDIR) $(BBHOME) $(BBHOME)/* $(BBHOME)/bin/* $(BBHOME)/etc/* $(BBHOME)/ext/* $(BBHOME)/web/* $(BBHOME)/www/gifs $(BBHOME)/www/gifs/* $(BBHOME)/www/menu $(BBHOME)/www/menu/* $(BBHOME)/www/help $(BBHOME)/www/help/* $(BBHOME)/www/notes $(BBHOME)/www/html $(BBHOME)/www/wml $(BBVAR) $(BBVAR)/* $(CGIDIR) $(CGIDIR)/*
	cd $(BBHOME); rm -f starthobbit,sh; ln -sf bin/starthobbit.sh .

