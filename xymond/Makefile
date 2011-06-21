PROGRAMS = xymon.sh xymond xymond_channel xymond_locator xymond_filestore xymond_history xymond_alert xymond_rrd xymond_sample xymond_client xymond_hostdata xymond_capture xymonfetch xymon-mailack trimhistory combostatus xymonreports.sh moverrd.sh convertnk rrdcachectl
CLIENTPROGRAMS = ../client/xymond_client

LIBOBJS = ../lib/libxymon.a ../lib/libredblack.a

XYMONDOBJS    = xymond.o xymond_buffer.o xymond_ipc.o
CHANNELOBJS   = xymond_channel.o xymond_buffer.o xymond_ipc.o
LOCATOROBJS   = xymond_locator.o
SAMPLEOBJS    = xymond_sample.o    xymond_worker.o xymond_buffer.o
FILESTOREOBJS = xymond_filestore.o xymond_worker.o xymond_buffer.o
HISTORYOBJS   = xymond_history.o   xymond_worker.o xymond_buffer.o
ALERTOBJS     = xymond_alert.o     xymond_worker.o xymond_buffer.o do_alert.o
RRDOBJS       = xymond_rrd.o       xymond_worker.o xymond_buffer.o do_rrd.o client_config.o
HOSTDATAOBJS  = xymond_hostdata.o  xymond_worker.o xymond_buffer.o
CAPTUREOBJS   = xymond_capture.o   xymond_worker.o xymond_buffer.o
CLIENTOBJS    = xymond_client.o    xymond_worker.o xymond_buffer.o client_config.o
COMBOTESTOBJS = combostatus.o
MAILACKOBJS   = xymon-mailack.o
TRIMHISTOBJS  = trimhistory.o
FETCHOBJS     = xymonfetch.o
CONVERTNKOBJS = convertnk.o
RRDCACHECTLOBJS = rrdcachectl.o

IDTOOL := $(shell if test `uname -s` = "SunOS"; then echo /usr/xpg4/bin/id; else echo id; fi)


all: $(PROGRAMS) cfgfiles

# Need NETLIBS on Solaris for getservbyname(), called by parse_url()

client: $(CLIENTPROGRAMS)

xymond: $(XYMONDOBJS) $(LIBOBJS)
	$(CC) $(LDFLAGS) -o $@ $(RPATHOPT) $(XYMONDOBJS) $(LIBOBJS) $(PCRELIBS) $(NETLIBS) $(LIBRTDEF)

xymond_channel: $(CHANNELOBJS) $(LIBOBJS)
	$(CC) $(LDFLAGS) -o $@ $(RPATHOPT) $(CHANNELOBJS) $(LIBOBJS) $(PCRELIBS) $(NETLIBS) $(LIBRTDEF)

xymond_locator: $(LOCATOROBJS) $(LIBOBJS)
	$(CC) $(LDFLAGS) -o $@ $(RPATHOPT) $(LOCATOROBJS) $(LIBOBJS) $(NETLIBS) $(LIBRTDEF)

xymond_filestore: $(FILESTOREOBJS) $(LIBOBJS)
	$(CC) $(LDFLAGS) -o $@ $(RPATHOPT) $(FILESTOREOBJS) $(LIBOBJS) $(PCRELIBS) $(NETLIBS) $(LIBRTDEF)

xymond_history: $(HISTORYOBJS) $(LIBOBJS)
	$(CC) $(LDFLAGS) -o $@ $(RPATHOPT) $(HISTORYOBJS) $(LIBOBJS) $(NETLIBS) $(LIBRTDEF)

xymond_alert: $(ALERTOBJS) $(LIBOBJS)
	$(CC) $(LDFLAGS) -o $@ $(RPATHOPT) $(ALERTOBJS) $(LIBOBJS) $(PCRELIBS) $(NETLIBS) $(LIBRTDEF)

xymond_rrd: $(RRDOBJS) $(LIBOBJS)
	$(CC) $(LDFLAGS) -o $@ $(RPATHOPT) $(RRDOBJS) $(LIBOBJS) $(RRDLIBS) $(PCRELIBS) $(NETLIBS) $(LIBRTDEF)

do_alert.o: do_alert.c
	$(CC) $(CFLAGS) $(PCREINCDIR) -c -o $@ do_alert.c

do_rrd.o: do_rrd.c do_rrd.h rrd/*.c
	$(CC) $(CFLAGS) $(RRDINCDIR) $(PCREINCDIR) $(RRDDEF) -c -o $@ do_rrd.c

xymond_capture.o: xymond_capture.c
	$(CC) $(CFLAGS) $(PCREINCDIR) -c -o $@ xymond_capture.c

xymond_sample: $(SAMPLEOBJS) $(LIBOBJS)
	$(CC) $(LDFLAGS) -o $@ $(RPATHOPT) $(SAMPLEOBJS) $(LIBOBJS) $(NETLIBS) $(LIBRTDEF)

xymond_capture: $(CAPTUREOBJS) $(LIBOBJS)
	$(CC) $(LDFLAGS) -o $@ $(RPATHOPT) $(CAPTUREOBJS) $(LIBOBJS) $(PCRELIBS) $(NETLIBS) $(LIBRTDEF)

xymond_hostdata: $(HOSTDATAOBJS) $(LIBOBJS)
	$(CC) $(LDFLAGS) -o $@ $(RPATHOPT) $(HOSTDATAOBJS) $(LIBOBJS) $(NETLIBS) $(LIBRTDEF)

xymond_client: $(CLIENTOBJS) $(LIBOBJS)
	$(CC) $(LDFLAGS) -o $@ $(RPATHOPT) $(CLIENTOBJS) $(LIBOBJS) $(PCRELIBS) $(NETLIBS) $(LIBRTDEF)

../client/xymond_client: $(CLIENTOBJS) ../lib/xymonclient.a
	$(CC) -o $@ $(RPATHOPT) $(CLIENTOBJS) ../lib/xymonclient.a $(PCRELIBS) $(NETLIBS) $(LIBRTDEF)

xymond_client.o: xymond_client.c client/*.c
	$(CC) $(CFLAGS) -c -o $@ xymond_client.c

combostatus.o: combostatus.c
	$(CC) $(CFLAGS) $(PCREINCDIR) -c -o $@ $<

combostatus: $(COMBOTESTOBJS) $(LIBOBJS)
	$(CC) $(LDFLAGS) -o $@ $(RPATHOPT) $(COMBOTESTOBJS) $(LIBOBJS) $(PCRELIBS) $(NETLIBS) $(LIBRTDEF)

xymon-mailack.o: xymon-mailack.c
	$(CC) $(CFLAGS) $(PCREINCDIR) -c -o $@ xymon-mailack.c

xymon-mailack: $(MAILACKOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(RPATHOPT) $(MAILACKOBJS) $(LIBOBJS) $(PCRELIBS) $(NETLIBS) $(LIBRTDEF)

trimhistory: $(TRIMHISTOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(RPATHOPT) $(TRIMHISTOBJS) $(LIBOBJS) $(NETLIBS) $(LIBRTDEF)

xymonfetch: $(FETCHOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(RPATHOPT) $(FETCHOBJS) $(LIBOBJS) $(NETLIBS) $(LIBRTDEF)

convertnk: $(CONVERTNKOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(RPATHOPT) $(CONVERTNKOBJS) $(LIBOBJS) $(NETLIBS) $(LIBRTDEF)

rrdcachectl: $(RRDCACHECTLOBJS) $(LIBOBJS)
	$(CC) $(CFLAGS) -o $@ $(RPATHOPT) $(RRDCACHECTLOBJS) $(LIBOBJS) $(NETLIBS) $(LIBRTDEF)

xymon.sh: xymon.sh.DIST
	cat $< | sed -e 's!@XYMONHOME@!$(XYMONHOME)!g' | sed -e 's!@XYMONLOGDIR@!$(XYMONLOGDIR)!g' | sed -e 's!@XYMONUSER@!$(XYMONUSER)!g' | sed -e 's!@RUNTIMEDEFS@!$(RUNTIMEDEFS)!g' >$@
	chmod 755 $@

xymonreports.sh: xymonreports.sh.DIST
	cat $< | sed -e 's!@XYMONHOME@!$(XYMONHOME)!g' >$@
	chmod 755 $@

moverrd.sh: moverrd.sh.DIST
	cat $< | sed -e 's!@XYMONHOME@!$(XYMONHOME)!g' | sed -e 's!@XYMONVAR@!$(XYMONVAR)!g' >$@
	chmod 755 $@

ifeq ($(XYMONCGIURL),$(SECUREXYMONCGIURL))
   APACHECONF = etcfiles/xymon-apache-open.DIST
else
   APACHECONF = etcfiles/xymon-apache-secure.DIST
endif

cfgfiles:
	cat $(APACHECONF) | sed -e 's!@XYMONHOME@!$(XYMONHOME)!g' | sed -e 's!@INSTALLETCDIR@!$(INSTALLETCDIR)!g' | sed -e 's!@INSTALLWWWDIR@!$(INSTALLWWWDIR)!g' | sed -e 's!@CGIDIR@!$(CGIDIR)!g' | sed -e 's!@SECURECGIDIR@!$(SECURECGIDIR)!g' | sed -e 's!@XYMONHOSTURL@!$(XYMONHOSTURL)!g' | sed -e 's!@XYMONCGIURL@!$(XYMONCGIURL)!g' | sed -e 's!@SECUREXYMONCGIURL@!$(SECUREXYMONCGIURL)!g' >etcfiles/xymon-apache.conf
	cat etcfiles/xymonserver.cfg.DIST | sed -e 's!@XYMONTOPDIR@!$(XYMONTOPDIR)!g'| sed -e 's!@XYMONLOGDIR@!$(XYMONLOGDIR)!g'| sed -e 's!@XYMONHOSTNAME@!$(XYMONHOSTNAME)!g'| sed -e 's!@XYMONHOSTIP@!$(XYMONHOSTIP)!g'| sed -e 's!@XYMONHOSTOS@!$(XYMONHOSTOS)!g' | sed -e 's!@XYMONHOSTURL@!$(XYMONHOSTURL)!g' | sed -e 's!@XYMONCGIURL@!$(XYMONCGIURL)!g' | sed -e 's!@SECUREXYMONCGIURL@!$(SECUREXYMONCGIURL)!g' | sed -e 's!@XYMONHOME@!$(XYMONHOME)!g' | sed -e 's!@XYMONVAR@!$(XYMONVAR)!g' | sed -e 's!@FPING@!$(FPING)!g' | sed -e 's!@MAILPROGRAM@!$(MAILPROGRAM)!g' | sed -e 's!@RUNTIMEDEFS@!$(RUNTIMEDEFS)!g' >etcfiles/xymonserver.cfg
	../build/bb-commands.sh >>etcfiles/xymonserver.cfg
	cat etcfiles/hosts.cfg.DIST | sed -e 's!@XYMONHOSTNAME@!$(XYMONHOSTNAME)!g' | sed -e 's!@XYMONHOSTIP@!$(XYMONHOSTIP)!g' >etcfiles/hosts.cfg
	cat etcfiles/alerts.cfg.DIST | sed -e 's!@XYMONHOSTNAME@!$(XYMONHOSTNAME)!g' | sed -e 's!@XYMONHOSTIP@!$(XYMONHOSTIP)!g' >etcfiles/alerts.cfg
	cat etcfiles/tasks.cfg.DIST | sed -e 's!@XYMONHOME@!$(XYMONHOME)!g' | sed -e 's!@XYMONTOPDIR@!$(XYMONTOPDIR)!g' >etcfiles/tasks.cfg
	cat etcfiles/cgioptions.cfg.DIST | sed -e 's!@XYMONHOME@!$(XYMONHOME)!g' >etcfiles/cgioptions.cfg

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(PROGRAMS) *.o *~ client/*~ rrd/*~
	rm -f etcfiles/xymon-apache.conf etcfiles/xymonserver.cfg etcfiles/hosts.cfg etcfiles/alerts.cfg etcfiles/tasks.cfg etcfiles/cgioptions.cfg
	(find etcfiles/ -name "*~"; find wwwfiles/ -name "*~"; find webfiles/ -name "*~") | xargs rm -f

install: install-bin install-man install-cfg

install-bin:
	cp -fp $(PROGRAMS) $(INSTALLROOT)$(INSTALLBINDIR)/

install-man:
	mkdir -p $(INSTALLROOT)$(MANROOT)/man1 $(INSTALLROOT)$(MANROOT)/man5 $(INSTALLROOT)$(MANROOT)/man8
	cp -fp *.1 $(INSTALLROOT)$(MANROOT)/man1/
	cp -fp *.5 $(INSTALLROOT)$(MANROOT)/man5/
	cp -fp *.8 $(INSTALLROOT)$(MANROOT)/man8/

install-cfg:
	cd etcfiles; ../../build/merge-lines xymonserver.cfg $(INSTALLROOT)$(INSTALLETCDIR)/xymonserver.cfg LARRDCOLUMN=TRENDSCOLUMN LARRDS=TEST2RRD
	cd etcfiles; ../../build/merge-lines cgioptions.cfg $(INSTALLROOT)$(INSTALLETCDIR)/cgioptions.cfg
	cd etcfiles; ../../build/merge-sects tasks.cfg $(INSTALLROOT)$(INSTALLETCDIR)/tasks.cfg larrdstatus=rrdstatus larrddata=rrddata
	cd etcfiles; ../../build/merge-sects client-local.cfg $(INSTALLROOT)$(INSTALLETCDIR)/client-local.cfg
	cd etcfiles; ../../build/merge-sects graphs.cfg $(INSTALLROOT)$(INSTALLETCDIR)/graphs.cfg
	cd etcfiles; ../../build/merge-lines columndoc.csv $(INSTALLROOT)$(INSTALLETCDIR)/columndoc.csv
	cd etcfiles; (echo "hosts.cfg"; echo "alerts.cfg"; echo "analysis.cfg"; echo "combo.cfg"; echo "client-local.cfg"; echo "holidays.cfg"; echo "rrddefinitions.cfg"; echo snmpmibs.cfg; echo xymonmenu.cfg) | ../../build/setup-newfiles $(INSTALLROOT)$(INSTALLETCDIR)/
	cd etcfiles; cp -fp xymon-apache.conf $(INSTALLROOT)$(INSTALLETCDIR)/xymon-apache.conf
	cd $(INSTALLROOT)$(XYMONHOME); rm -f xymon.sh; ln -sf bin/xymon.sh .
	cd wwwfiles; find . | grep -v RCS | grep -v ".svn" | grep -v DIST | ../../build/setup-newfiles $(INSTALLROOT)$(INSTALLWWWDIR)/ ../../build/md5.dat
	cd webfiles; find . | grep -v RCS | grep -v ".svn" | grep -v DIST | ../../build/setup-newfiles $(INSTALLROOT)$(INSTALLWEBDIR)/ ../../build/md5.dat
	touch $(INSTALLROOT)$(INSTALLETCDIR)/critical.cfg $(INSTALLROOT)$(INSTALLETCDIR)/critical.cfg.bak
	chmod g+w $(INSTALLROOT)$(INSTALLETCDIR)/critical.cfg $(INSTALLROOT)$(INSTALLETCDIR)/critical.cfg.bak
	mkdir -p $(INSTALLROOT)$(XYMONLOGDIR)
ifndef PKGBUILD
	chown $(XYMONUSER) $(INSTALLROOT)$(XYMONLOGDIR) $(INSTALLROOT)$(XYMONHOME) $(INSTALLROOT)$(XYMONHOME)/* $(INSTALLROOT)$(INSTALLBINDIR)/* $(INSTALLROOT)$(INSTALLETCDIR)/* $(INSTALLROOT)$(INSTALLEXTDIR)/* $(INSTALLROOT)$(INSTALLWEBDIR)/* $(INSTALLROOT)$(INSTALLWWWDIR)/gifs $(INSTALLROOT)$(INSTALLWWWDIR)/gifs/* $(INSTALLROOT)$(INSTALLWWWDIR)/menu $(INSTALLROOT)$(INSTALLWWWDIR)/menu/* $(INSTALLROOT)$(INSTALLWWWDIR)/help $(INSTALLROOT)$(INSTALLWWWDIR)/notes $(INSTALLROOT)$(INSTALLWWWDIR)/html $(INSTALLROOT)$(INSTALLWWWDIR)/wml $(INSTALLROOT)$(XYMONVAR) $(INSTALLROOT)$(XYMONVAR)/*
	chgrp `$(IDTOOL) -g $(XYMONUSER)` $(INSTALLROOT)$(XYMONLOGDIR) $(INSTALLROOT)$(XYMONHOME) $(INSTALLROOT)$(XYMONHOME)/* $(INSTALLROOT)$(INSTALLBINDIR)/* $(INSTALLROOT)$(INSTALLETCDIR)/* $(INSTALLROOT)$(INSTALLEXTDIR)/* $(INSTALLROOT)$(INSTALLWEBDIR)/* $(INSTALLROOT)$(INSTALLWWWDIR)/gifs $(INSTALLROOT)$(INSTALLWWWDIR)/gifs/* $(INSTALLROOT)$(INSTALLWWWDIR)/menu $(INSTALLROOT)$(INSTALLWWWDIR)/menu/* $(INSTALLROOT)$(INSTALLWWWDIR)/help $(INSTALLROOT)$(INSTALLWWWDIR)/notes $(INSTALLROOT)$(INSTALLWWWDIR)/html $(INSTALLROOT)$(INSTALLWWWDIR)/wml $(INSTALLROOT)$(XYMONVAR) $(INSTALLROOT)$(XYMONVAR)/*
	chgrp $(HTTPDGID) $(INSTALLROOT)$(INSTALLETCDIR)/critical.cfg $(INSTALLROOT)$(INSTALLETCDIR)/critical.cfg.bak

	chown root $(INSTALLROOT)$(INSTALLBINDIR)/xymonping || echo "Could not make xymonping owned by root, continuing"
	chmod 4755 $(INSTALLROOT)$(INSTALLBINDIR)/xymonping || echo "Could not make xymonping suid-root, continuing"
endif



