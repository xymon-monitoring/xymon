PROGRAMS = xymon.sh hobbitd hobbitd_channel hobbitd_locator hobbitd_filestore hobbitd_history hobbitd_alert hobbitd_rrd hobbitd_sample hobbitd_client hobbitd_hostdata hobbitd_capture hobbitfetch hobbit-mailack trimhistory bbcombotest hobbitreports.sh moverrd.sh convertnk rrdcachectl
CLIENTPROGRAMS = ../client/hobbitd_client

LIBOBJS = ../lib/libbbgen.a

HOBBITDOBJS   = hobbitd.o hobbitd_buffer.o hobbitd_ipc.o
CHANNELOBJS   = hobbitd_channel.o hobbitd_buffer.o hobbitd_ipc.o
LOCATOROBJS   = hobbitd_locator.o
SAMPLEOBJS    = hobbitd_sample.o    hobbitd_worker.o hobbitd_buffer.o
FILESTOREOBJS = hobbitd_filestore.o hobbitd_worker.o hobbitd_buffer.o
HISTORYOBJS   = hobbitd_history.o   hobbitd_worker.o hobbitd_buffer.o
ALERTOBJS     = hobbitd_alert.o     hobbitd_worker.o hobbitd_buffer.o do_alert.o
RRDOBJS       = hobbitd_rrd.o       hobbitd_worker.o hobbitd_buffer.o do_rrd.o client_config.o
HOSTDATAOBJS  = hobbitd_hostdata.o  hobbitd_worker.o hobbitd_buffer.o
CAPTUREOBJS   = hobbitd_capture.o   hobbitd_worker.o hobbitd_buffer.o
CLIENTOBJS    = hobbitd_client.o    hobbitd_worker.o hobbitd_buffer.o client_config.o
COMBOTESTOBJS = bbcombotest.o
MAILACKOBJS   = hobbit-mailack.o
TRIMHISTOBJS  = trimhistory.o
FETCHOBJS     = hobbitfetch.o
CONVERTNKOBJS = convertnk.o
RRDCACHECTLOBJS = rrdcachectl.o

IDTOOL := $(shell if test `uname -s` = "SunOS"; then echo /usr/xpg4/bin/id; else echo id; fi)


all: $(PROGRAMS) cfgfiles

# Need NETLIBS on Solaris for getservbyname(), called by parse_url()

client: $(CLIENTPROGRAMS)

hobbitd: $(HOBBITDOBJS) $(LIBOBJS)
	$(CC) $(LDFLAGS) -o $@ $(RPATHOPT) $(HOBBITDOBJS) $(LIBOBJS) $(PCRELIBS) $(NETLIBS) $(LIBRTDEF)

hobbitd_channel: $(CHANNELOBJS) $(LIBOBJS)
	$(CC) $(LDFLAGS) -o $@ $(RPATHOPT) $(CHANNELOBJS) $(LIBOBJS) $(PCRELIBS) $(NETLIBS) $(LIBRTDEF)

hobbitd_locator: $(LOCATOROBJS) $(LIBOBJS)
	$(CC) $(LDFLAGS) -o $@ $(RPATHOPT) $(LOCATOROBJS) $(LIBOBJS) $(NETLIBS) $(LIBRTDEF)

hobbitd_filestore: $(FILESTOREOBJS) $(LIBOBJS)
	$(CC) $(LDFLAGS) -o $@ $(RPATHOPT) $(FILESTOREOBJS) $(LIBOBJS) $(PCRELIBS) $(NETLIBS) $(LIBRTDEF)

hobbitd_history: $(HISTORYOBJS) $(LIBOBJS)
	$(CC) $(LDFLAGS) -o $@ $(RPATHOPT) $(HISTORYOBJS) $(LIBOBJS) $(NETLIBS) $(LIBRTDEF)

hobbitd_alert: $(ALERTOBJS) $(LIBOBJS)
	$(CC) $(LDFLAGS) -o $@ $(RPATHOPT) $(ALERTOBJS) $(LIBOBJS) $(PCRELIBS) $(NETLIBS) $(LIBRTDEF)

hobbitd_rrd: $(RRDOBJS) $(LIBOBJS)
	$(CC) $(LDFLAGS) -o $@ $(RPATHOPT) $(RRDOBJS) $(LIBOBJS) $(RRDLIBS) $(PCRELIBS) $(NETLIBS) $(LIBRTDEF)

do_alert.o: do_alert.c
	$(CC) $(CFLAGS) $(PCREINCDIR) -c -o $@ do_alert.c

do_rrd.o: do_rrd.c do_rrd.h rrd/*.c
	$(CC) $(CFLAGS) $(RRDINCDIR) $(PCREINCDIR) $(RRDDEF) -c -o $@ do_rrd.c

hobbitd_capture.o: hobbitd_capture.c
	$(CC) $(CFLAGS) $(PCREINCDIR) -c -o $@ hobbitd_capture.c

hobbitd_sample: $(SAMPLEOBJS) $(LIBOBJS)
	$(CC) $(LDFLAGS) -o $@ $(RPATHOPT) $(SAMPLEOBJS) $(LIBOBJS) $(NETLIBS) $(LIBRTDEF)

hobbitd_capture: $(CAPTUREOBJS) $(LIBOBJS)
	$(CC) $(LDFLAGS) -o $@ $(RPATHOPT) $(CAPTUREOBJS) $(LIBOBJS) $(PCRELIBS) $(NETLIBS) $(LIBRTDEF)

hobbitd_hostdata: $(HOSTDATAOBJS) $(LIBOBJS)
	$(CC) $(LDFLAGS) -o $@ $(RPATHOPT) $(HOSTDATAOBJS) $(LIBOBJS) $(NETLIBS) $(LIBRTDEF)

hobbitd_client: $(CLIENTOBJS) $(LIBOBJS)
	$(CC) $(LDFLAGS) -o $@ $(RPATHOPT) $(CLIENTOBJS) $(LIBOBJS) $(PCRELIBS) $(NETLIBS) $(LIBRTDEF)

../client/hobbitd_client: $(CLIENTOBJS) ../lib/hobbitclient.a
	$(CC) -o $@ $(RPATHOPT) $(CLIENTOBJS) ../lib/hobbitclient.a $(PCRELIBS) $(NETLIBS) $(LIBRTDEF)

hobbitd_client.o: hobbitd_client.c client/*.c
	$(CC) $(CFLAGS) -c -o $@ hobbitd_client.c

bbcombotest.o: bbcombotest.c
	$(CC) $(CFLAGS) $(PCREINCDIR) -c -o $@ $<

bbcombotest: $(COMBOTESTOBJS) $(LIBOBJS)
	$(CC) $(LDFLAGS) -o $@ $(RPATHOPT) $(COMBOTESTOBJS) $(LIBOBJS) $(PCRELIBS) $(NETLIBS) $(LIBRTDEF)

hobbit-mailack.o: hobbit-mailack.c
	$(CC) $(CFLAGS) $(PCREINCDIR) -c -o $@ hobbit-mailack.c

hobbit-mailack: $(MAILACKOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(RPATHOPT) $(MAILACKOBJS) $(LIBOBJS) $(PCRELIBS) $(NETLIBS) $(LIBRTDEF)

trimhistory: $(TRIMHISTOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(RPATHOPT) $(TRIMHISTOBJS) $(LIBOBJS) $(NETLIBS) $(LIBRTDEF)

hobbitfetch: $(FETCHOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(RPATHOPT) $(FETCHOBJS) $(LIBOBJS) $(NETLIBS) $(LIBRTDEF)

convertnk: $(CONVERTNKOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(RPATHOPT) $(CONVERTNKOBJS) $(LIBOBJS) $(NETLIBS) $(LIBRTDEF)

rrdcachectl: $(RRDCACHECTLOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(RPATHOPT) $(RRDCACHECTLOBJS) $(LIBOBJS) $(NETLIBS) $(LIBRTDEF)

xymon.sh: xymon.sh.DIST
	cat $< | sed -e 's!@BBHOME@!$(BBHOME)!g' | sed -e 's!@BBLOGDIR@!$(BBLOGDIR)!g' | sed -e 's!@BBUSER@!$(BBUSER)!g' | sed -e 's!@RUNTIMEDEFS@!$(RUNTIMEDEFS)!g' >$@
	chmod 755 $@

hobbitreports.sh: hobbitreports.sh.DIST
	cat $< | sed -e 's!@BBHOME@!$(BBHOME)!g' >$@
	chmod 755 $@

moverrd.sh: moverrd.sh.DIST
	cat $< | sed -e 's!@BBHOME@!$(BBHOME)!g' | sed -e 's!@BBVAR@!$(BBVAR)!g' >$@
	chmod 755 $@

ifeq ($(BBCGIURL),$(SECUREBBCGIURL))
   APACHECONF = etcfiles/xymon-apache-open.DIST
else
   APACHECONF = etcfiles/xymon-apache-secure.DIST
endif

cfgfiles:
	cat $(APACHECONF) | sed -e 's!@BBHOME@!$(BBHOME)!g' | sed -e 's!@INSTALLETCDIR@!$(INSTALLETCDIR)!g' | sed -e 's!@INSTALLWWWDIR@!$(INSTALLWWWDIR)!g' | sed -e 's!@CGIDIR@!$(CGIDIR)!g' | sed -e 's!@SECURECGIDIR@!$(SECURECGIDIR)!g' | sed -e 's!@BBHOSTURL@!$(BBHOSTURL)!g' | sed -e 's!@BBCGIURL@!$(BBCGIURL)!g' | sed -e 's!@SECUREBBCGIURL@!$(SECUREBBCGIURL)!g' >etcfiles/xymon-apache.conf
	cat etcfiles/hobbitserver.cfg.DIST | sed -e 's!@BBTOPDIR@!$(BBTOPDIR)!g'| sed -e 's!@BBLOGDIR@!$(BBLOGDIR)!g'| sed -e 's!@BBHOSTNAME@!$(BBHOSTNAME)!g'| sed -e 's!@BBHOSTIP@!$(BBHOSTIP)!g'| sed -e 's!@BBHOSTOS@!$(BBHOSTOS)!g' | sed -e 's!@BBHOSTURL@!$(BBHOSTURL)!g' | sed -e 's!@BBCGIURL@!$(BBCGIURL)!g' | sed -e 's!@SECUREBBCGIURL@!$(SECUREBBCGIURL)!g' | sed -e 's!@BBHOME@!$(BBHOME)!g' | sed -e 's!@BBVAR@!$(BBVAR)!g' | sed -e 's!@FPING@!$(FPING)!g' | sed -e 's!@MAILPROGRAM@!$(MAILPROGRAM)!g' | sed -e 's!@RUNTIMEDEFS@!$(RUNTIMEDEFS)!g' >etcfiles/hobbitserver.cfg
	../build/bb-commands.sh >>etcfiles/hobbitserver.cfg
	cat etcfiles/bb-hosts.DIST | sed -e 's!@BBHOSTNAME@!$(BBHOSTNAME)!g' | sed -e 's!@BBHOSTIP@!$(BBHOSTIP)!g' >etcfiles/bb-hosts
	cat etcfiles/hobbit-alerts.cfg.DIST | sed -e 's!@BBHOSTNAME@!$(BBHOSTNAME)!g' | sed -e 's!@BBHOSTIP@!$(BBHOSTIP)!g' >etcfiles/hobbit-alerts.cfg
	cat etcfiles/hobbitlaunch.cfg.DIST | sed -e 's!@BBHOME@!$(BBHOME)!g' | sed -e 's!@BBTOPDIR@!$(BBTOPDIR)!g' >etcfiles/hobbitlaunch.cfg
	cat etcfiles/hobbitcgi.cfg.DIST | sed -e 's!@BBHOME@!$(BBHOME)!g' >etcfiles/hobbitcgi.cfg

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(PROGRAMS) *.o *~ client/*~ rrd/*~
	rm -f etcfiles/xymon-apache.conf etcfiles/hobbitserver.cfg etcfiles/bb-hosts etcfiles/hobbit-alerts.cfg etcfiles/hobbitlaunch.cfg etcfiles/hobbitcgi.cfg
	(find etcfiles/ -name "*~"; find wwwfiles/ -name "*~"; find webfiles/ -name "*~") | xargs rm -f

install: install-bin install-man install-cfg install-obsoleted

install-bin:
	cp -fp $(PROGRAMS) $(INSTALLROOT)$(INSTALLBINDIR)/

install-man:
	mkdir -p $(INSTALLROOT)$(MANROOT)/man1 $(INSTALLROOT)$(MANROOT)/man5 $(INSTALLROOT)$(MANROOT)/man8
	cp -fp *.1 $(INSTALLROOT)$(MANROOT)/man1/
	cp -fp *.5 $(INSTALLROOT)$(MANROOT)/man5/
	cp -fp *.8 $(INSTALLROOT)$(MANROOT)/man8/

install-cfg:
	cd etcfiles; ../../build/merge-lines hobbitserver.cfg $(INSTALLROOT)$(INSTALLETCDIR)/hobbitserver.cfg LARRDCOLUMN=TRENDSCOLUMN LARRDS=TEST2RRD
	cd etcfiles; ../../build/merge-lines hobbitcgi.cfg $(INSTALLROOT)$(INSTALLETCDIR)/hobbitcgi.cfg
	cd etcfiles; ../../build/merge-sects hobbitlaunch.cfg $(INSTALLROOT)$(INSTALLETCDIR)/hobbitlaunch.cfg larrdstatus=rrdstatus larrddata=rrddata
	cd etcfiles; ../../build/merge-sects client-local.cfg $(INSTALLROOT)$(INSTALLETCDIR)/client-local.cfg
	cd etcfiles; ../../build/merge-sects hobbitgraph.cfg $(INSTALLROOT)$(INSTALLETCDIR)/hobbitgraph.cfg
	cd etcfiles; ../../build/merge-lines columndoc.csv $(INSTALLROOT)$(INSTALLETCDIR)/columndoc.csv
	cd etcfiles; (echo "bb-hosts"; echo "hobbit-alerts.cfg"; echo "hobbit-clients.cfg"; echo "bbcombotest.cfg"; echo "client-local.cfg"; echo "hobbit-holidays.cfg"; echo "hobbit-rrddefinitions.cfg"; echo hobbit-snmpmibs.cfg; echo xymonmenu.cfg) | ../../build/setup-newfiles $(INSTALLROOT)$(INSTALLETCDIR)/
	cd etcfiles; cp -fp xymon-apache.conf $(INSTALLROOT)$(INSTALLETCDIR)/xymon-apache.conf
	cd $(INSTALLROOT)$(BBHOME); rm -f xymon.sh; ln -sf bin/xymon.sh .
	cd wwwfiles; find . | grep -v RCS | grep -v DIST | ../../build/setup-newfiles $(INSTALLROOT)$(INSTALLWWWDIR)/ ../../build/md5.dat
	cd webfiles; find . | grep -v RCS | grep -v DIST | ../../build/setup-newfiles $(INSTALLROOT)$(INSTALLWEBDIR)/ ../../build/md5.dat
	touch $(INSTALLROOT)$(INSTALLETCDIR)/hobbit-nkview.cfg $(INSTALLROOT)$(INSTALLETCDIR)/hobbit-nkview.cfg.bak
	chmod g+w $(INSTALLROOT)$(INSTALLETCDIR)/hobbit-nkview.cfg $(INSTALLROOT)$(INSTALLETCDIR)/hobbit-nkview.cfg.bak
	mkdir -p $(INSTALLROOT)$(BBLOGDIR)
ifndef PKGBUILD
	chown $(BBUSER) $(INSTALLROOT)$(BBLOGDIR) $(INSTALLROOT)$(BBHOME) $(INSTALLROOT)$(BBHOME)/* $(INSTALLROOT)$(INSTALLBINDIR)/* $(INSTALLROOT)$(INSTALLETCDIR)/* $(INSTALLROOT)$(INSTALLEXTDIR)/* $(INSTALLROOT)$(INSTALLWEBDIR)/* $(INSTALLROOT)$(INSTALLWWWDIR)/gifs $(INSTALLROOT)$(INSTALLWWWDIR)/gifs/* $(INSTALLROOT)$(INSTALLWWWDIR)/menu $(INSTALLROOT)$(INSTALLWWWDIR)/menu/* $(INSTALLROOT)$(INSTALLWWWDIR)/help $(INSTALLROOT)$(INSTALLWWWDIR)/notes $(INSTALLROOT)$(INSTALLWWWDIR)/html $(INSTALLROOT)$(INSTALLWWWDIR)/wml $(INSTALLROOT)$(BBVAR) $(INSTALLROOT)$(BBVAR)/*
	chgrp `$(IDTOOL) -g $(BBUSER)` $(INSTALLROOT)$(BBLOGDIR) $(INSTALLROOT)$(BBHOME) $(INSTALLROOT)$(BBHOME)/* $(INSTALLROOT)$(INSTALLBINDIR)/* $(INSTALLROOT)$(INSTALLETCDIR)/* $(INSTALLROOT)$(INSTALLEXTDIR)/* $(INSTALLROOT)$(INSTALLWEBDIR)/* $(INSTALLROOT)$(INSTALLWWWDIR)/gifs $(INSTALLROOT)$(INSTALLWWWDIR)/gifs/* $(INSTALLROOT)$(INSTALLWWWDIR)/menu $(INSTALLROOT)$(INSTALLWWWDIR)/menu/* $(INSTALLROOT)$(INSTALLWWWDIR)/help $(INSTALLROOT)$(INSTALLWWWDIR)/notes $(INSTALLROOT)$(INSTALLWWWDIR)/html $(INSTALLROOT)$(INSTALLWWWDIR)/wml $(INSTALLROOT)$(BBVAR) $(INSTALLROOT)$(BBVAR)/*
	chgrp $(HTTPDGID) $(INSTALLROOT)$(INSTALLETCDIR)/hobbit-nkview.cfg $(INSTALLROOT)$(INSTALLETCDIR)/hobbit-nkview.cfg.bak

	chown root $(INSTALLROOT)$(INSTALLBINDIR)/hobbitping || echo "Could not make hobbitping owned by root, continuing"
	chmod 4755 $(INSTALLROOT)$(INSTALLBINDIR)/hobbitping || echo "Could not make hobbitping suid-root, continuing"
endif

install-obsoleted:
	if test -f $(INSTALLROOT)$(INSTALLBINDIR)/hobbitd_larrd; then rm -f $(INSTALLROOT)$(INSTALLBINDIR)/hobbitd_larrd; fi
	cd $(INSTALLROOT)$(INSTALLBINDIR) && ln -s hobbitd_rrd hobbitd_larrd


