PROGRAMS = hobbitd hobbitd_channel hobbitd_filter hobbitd_filestore hobbitd_history hobbitd_alert hobbitd_larrd hobbitd_sample starthobbit.sh hobbitgraph.cgi hobbit-mailack
CGISCRIPTS = hobbitgraph.sh
SECCGISCRIPTS = bb-ack.sh maint.pl

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
MAILACKOBJS   = hobbit-mailack.o

IDTOOL := $(shell if test `uname -s` = "SunOS"; then echo /usr/xpg4/bin/id; else echo id; fi)


all: $(PROGRAMS) $(CGISCRIPTS) $(SECCGISCRIPTS) cfgfiles moverrd.sh

# Need NETLIBS on Solaris for getservbyname(), called by parse_url()

hobbitd: $(HOBBITDOBJS) $(LIBOBJS)
	$(CC) -o $@ $(RPATHOPT) $(HOBBITDOBJS) $(LIBOBJS) $(NETLIBS)

hobbitd_channel: $(CHANNELOBJS) $(LIBOBJS)
	$(CC) -o $@ $(RPATHOPT) $(CHANNELOBJS) $(LIBOBJS)

hobbitd_filestore: $(FILESTOREOBJS) $(LIBOBJS)
	$(CC) -o $@ $(RPATHOPT) $(FILESTOREOBJS) $(LIBOBJS) $(NETLIBS)

hobbitd_history: $(HISTORYOBJS) $(LIBOBJS)
	$(CC) -o $@ $(RPATHOPT) $(HISTORYOBJS) $(LIBOBJS)

hobbitd_alert: $(ALERTOBJS) $(LIBOBJS)
	$(CC) -o $@ $(RPATHOPT) $(ALERTOBJS) $(LIBOBJS) $(PCRELIBS)

hobbitd_larrd: $(LARRDOBJS) $(LIBOBJS)
	$(CC) -o $@ $(RPATHOPT) $(LARRDOBJS) $(LIBOBJS) $(RRDLIBS) $(NETLIBS)

hobbitd_filter: $(FILTEROBJS) $(LIBOBJS)
	$(CC) -o $@ $(RPATHOPT) $(FILTEROBJS) $(LIBOBJS)

do_alert.o: do_alert.c
	$(CC) $(CFLAGS) $(PCREINCDIR) -c -o $@ do_alert.c

do_larrd.o: do_larrd.c do_larrd.h larrd/*.c
	$(CC) $(CFLAGS) $(RRDINCDIR) -c -o $@ do_larrd.c

hobbitd_sample: $(SAMPLEOBJS) $(LIBOBJS)
	$(CC) -o $@ $(RPATHOPT) $(SAMPLEOBJS) $(LIBOBJS)

hobbitgraph.o: hobbitgraph.c
	$(CC) $(CFLAGS) $(PCREINCDIR) $(RRDINCDIR) -c -o $@ hobbitgraph.c

hobbitgraph.cgi: $(HOBBITGRAPHOBJS)
	$(CC) $(CFLAGS) -o $@ $(RPATHOPT) $(HOBBITGRAPHOBJS) $(LIBOBJS) $(PCRELIBS) $(RRDLIBS) $(NETLIBS)

hobbit-mailack.o: hobbit-mailack.c
	$(CC) $(CFLAGS) $(PCREINCDIR) -c -o $@ hobbit-mailack.c

hobbit-mailack: $(MAILACKOBJS)
	$(CC) $(CFLAGS) -o $@ $(RPATHOPT) $(MAILACKOBJS) $(LIBOBJS) $(PCRELIBS) $(NETLIBS)

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
	cat $< | sed -e 's!@BBHOME@!$(BBHOME)!g' | sed -e 's!@BBVAR@!$(BBVAR)!g' | sed -e 's!@PERLBINARY@!$(PERLBINARY)!g' >$@
	chmod 755 $@

ifeq ($(BBCGIURL),$(SECUREBBCGIURL))
   APACHECONF = etcfiles/hobbit-apache-open.DIST
else
   APACHECONF = etcfiles/hobbit-apache-secure.DIST
endif

cfgfiles:
	cat $(APACHECONF) | sed -e 's!@BBHOME@!$(BBHOME)!g' | sed -e 's!@CGIDIR@!$(CGIDIR)!g' | sed -e 's!@SECURECGIDIR@!$(SECURECGIDIR)!g' | sed -e 's!@BBHOSTURL@!$(BBHOSTURL)!g' | sed -e 's!@BBCGIURL@!$(BBCGIURL)!g' | sed -e 's!@SECUREBBCGIURL@!$(SECUREBBCGIURL)!g' >etcfiles/hobbit-apache.conf
	cat etcfiles/hobbitserver.cfg.DIST | sed -e 's!@BBTOPDIR@!$(BBTOPDIR)!g'| sed -e 's!@BBLOGDIR@!$(BBLOGDIR)!g'| sed -e 's!@BBHOSTNAME@!$(BBHOSTNAME)!g'| sed -e 's!@BBHOSTIP@!$(BBHOSTIP)!g'| sed -e 's!@BBHOSTOS@!$(BBHOSTOS)!g' | sed -e 's!@BBHOSTURL@!$(BBHOSTURL)!g' | sed -e 's!@BBCGIURL@!$(BBCGIURL)!g' | sed -e 's!@SECUREBBCGIURL@!$(SECUREBBCGIURL)!g' | sed -e 's!@BBHOME@!$(BBHOME)!g' | sed -e 's!@BBVAR@!$(BBVAR)!g' | sed -e 's!@FPING@!$(FPING)!g' | sed -e 's!@RUNTIMEDEFS@!$(RUNTIMEDEFS)!g' >etcfiles/hobbitserver.cfg
	../scripts/bb-commands.sh >>etcfiles/hobbitserver.cfg
	cat etcfiles/bb-hosts.DIST | sed -e 's!@BBHOSTNAME@!$(BBHOSTNAME)!g'| sed -e 's!@BBHOSTIP@!$(BBHOSTIP)!g' >etcfiles/bb-hosts
	cat etcfiles/hobbit-alerts.cfg.DIST >etcfiles/hobbit-alerts.cfg
	cat etcfiles/hobbitlaunch.cfg.DIST | sed -e 's!@BBHOME@!$(BBHOME)!g' >etcfiles/hobbitlaunch.cfg
	cat wwwfiles/menu/menu_items.js.DIST | sed -e 's!@BBHOSTURL@!$(BBHOSTURL)!g' | sed -e 's!@BBCGIURL@!$(BBCGIURL)!g' | sed -e 's!@SECUREBBCGIURL@!$(SECUREBBCGIURL)!g' >wwwfiles/menu/menu_items.js

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(PROGRAMS) $(CGISCRIPTS) $(SECCGISCRIPTS) *.o *~ larrd/*~ moverrd.sh
	rm -f etcfiles/hobbit-apache.conf etcfiles/hobbitserver.cfg etcfiles/bb-hosts etcfiles/hobbit-alerts.cfg etcfiles/hobbitlaunch.cfg
	rm -f wwwfiles/menu/menu_items.js
	(find etcfiles/ -name "*~"; find wwwfiles/ -name "*~"; find webfiles/ -name "*~") | xargs rm -f

install: install-bin install-cgi install-man install-cfg

install-bin:
	cp -fp $(PROGRAMS) $(BBHOME)/bin/

install-cgi:
	mkdir -p $(CGIDIR)
	cp -fp $(CGISCRIPTS) $(CGIDIR)/
	mkdir -p $(SECURECGIDIR)
	cp -fp $(SECCGISCRIPTS) $(SECURECGIDIR)/

install-man:
	mkdir -p $(MANROOT)/man1 $(MANROOT)/man5 $(MANROOT)/man8
	cp -fp *.1 $(MANROOT)/man1/
	cp -fp *.5 $(MANROOT)/man5/
	cp -fp *.8 $(MANROOT)/man8/

install-cfg:
	cd etcfiles; ../../build/merge-lines hobbitserver.cfg $(BBHOME)/etc/hobbitserver.cfg
	cd etcfiles; ../../build/merge-sects hobbitlaunch.cfg $(BBHOME)/etc/hobbitlaunch.cfg
	cd etcfiles; ../../build/merge-sects hobbitgraph.cfg $(BBHOME)/etc/hobbitgraph.cfg
	cd etcfiles; ../../build/merge-lines columndoc.csv $(BBHOME)/etc/columndoc.csv
	cd etcfiles; (echo "bb-hosts"; echo "hobbit-alerts.cfg") | ../../build/setup-newfiles $(BBHOME)/etc/
	cd etcfiles; cp -fp hobbit-apache.conf $(BBHOME)/etc/hobbit-apache.conf
	cd $(BBHOME); rm -f starthobbit.sh; ln -sf bin/starthobbit.sh .
	cd wwwfiles; find . | grep -v RCS | grep -v DIST | ../../build/setup-newfiles $(BBHOME)/www/ ../../build/md5.dat
	cd webfiles; find . | grep -v RCS | grep -v DIST | ../../build/setup-newfiles $(BBHOME)/web/ ../../build/md5.dat
	mkdir -p $(BBLOGDIR)
	chown $(BBUSER) $(BBLOGDIR) $(BBHOME) $(BBHOME)/* $(BBHOME)/bin/* $(BBHOME)/etc/* $(BBHOME)/ext/* $(BBHOME)/web/* $(BBHOME)/www/gifs $(BBHOME)/www/gifs/* $(BBHOME)/www/menu $(BBHOME)/www/menu/* $(BBHOME)/www/help $(BBHOME)/www/notes $(BBHOME)/www/html $(BBHOME)/www/wml $(BBVAR) $(BBVAR)/* $(CGIDIR) $(CGIDIR)/* $(SECURECGIDIR) $(SECURECGIDIR)/*
	chgrp `$(IDTOOL) -g $(BBUSER)` $(BBLOGDIR) $(BBHOME) $(BBHOME)/* $(BBHOME)/bin/* $(BBHOME)/etc/* $(BBHOME)/ext/* $(BBHOME)/web/* $(BBHOME)/www/gifs $(BBHOME)/www/gifs/* $(BBHOME)/www/menu $(BBHOME)/www/menu/* $(BBHOME)/www/help $(BBHOME)/www/notes $(BBHOME)/www/html $(BBHOME)/www/wml $(BBVAR) $(BBVAR)/* $(CGIDIR) $(CGIDIR)/* $(SECURECGIDIR) $(SECURECGIDIR)/*

