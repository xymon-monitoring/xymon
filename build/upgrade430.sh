#!/bin/sh

renameandlink()
{
	if test -f $1
	then
		mv $1 $2
		ln -s $2 $1
	fi
}

if test "$BBHOME" = ""
then
	echo "ERROR: This must be invoked using the bbcmd utility"
	echo "   bbcmd $0"
	exit 1
fi

# Get directory where we are running from
SDIR=`dirname $0`	# Could be a relative path
SDIRFULL=`(cd $SDIR; pwd)`

if test ! -f $SDIRFULL/renamevars
then
	echo "ERROR: You must run 'make' first to build the 4.3.0 tools"
	exit 1
fi

cd $BBHOME/etc || (echo "Cannot cd to etc directory"; exit 1)
if test ! -w .; then echo "Cannot write to etc directory"; exit 1; fi
renameandlink bb-hosts hosts.cfg
renameandlink bbcombotest.cfg combo.cfg
renameandlink hobbit-alerts.cfg alerts.cfg
renameandlink hobbit-nkview.cfg critical.cfg
renameandlink hobbit-nkview.cfg.bak critical.cfg.bak
renameandlink hobbit-rrddefinitions.cfg rrddefinitions.cfg
renameandlink hobbitgraph.cfg graphs.cfg
renameandlink hobbit-holidays.cfg holidays.cfg
renameandlink hobbit-clients.cfg analysis.cfg
renameandlink hobbit-snmpmibs.cfg snmpmibs.cfg
renameandlink hobbit-apache.conf xymon-apache.conf
renameandlink bb-services protocols.cfg
renameandlink hobbitcgi.cfg cgioptions.cfg

if test -f hobbitlaunch.cfg
then
	mv hobbitlaunch.cfg tasks.old
	$SDIRFULL/renametasks <tasks.old >tasks.cfg
	ln -s tasks.cfg hobbitlaunch.cfg
fi

if test -f hobbitserver.cfg
then
	mv hobbitserver.cfg xymonserver.old
	$SDIRFULL/renamevars >xymonserver.cfg <<EOF1
XYMONACKDIR=BBACKS
XYMONALLHISTLOG=BBALLHISTLOG
XYMONDATADIR=BBDATA
XYMONDATEFORMAT=BBDATEFORMAT
XYMONDISABLEDDIR=BBDISABLED
XYMSERVERS=BBDISPLAYS
XYMON=BB
XYMSRV=BBDISP
XYMONGENOPTS=BBGENOPTS
XYMONGENREPOPTS=BBGENREPOPTS
XYMONGENSNAPOPTS=BBGENSNAPOPTS
XYMONHELPSKIN=BBHELPSKIN
XYMONHISTEXT=BBHISTEXT
XYMONHISTLOGS=BBHISTLOGS
XYMONHISTDIR=BBHIST
XYMONHOME=BBHOME
XYMONHOSTHISTLOG=BBHOSTHISTLOG
HOSTSCFG=BBHOSTS
XYMONHTMLSTATUSDIR=BBHTML
XYMONLOGSTATUS=BBLOGSTATUS
XYMONRAWSTATUSDIR=BBLOGS
MAXMSGSPERCOMBO=BBMAXMSGSPERCOMBO
XYMONMENUSKIN=BBMENUSKIN
XYMONNOTESSKIN=BBNOTESSKIN
XYMONNOTESDIR=BBNOTES
SERVEROSTYPE=BBOSTYPE
XYMONREPEXT=BBREPEXT
XYMONREPGREEN=BBREPGREEN
XYMONREPURL=BBREPURL
XYMONREPWARN=BBREPWARN
XYMONREPDIR=BBREP
XYMONROUTERTEXT=BBROUTERTEXT
XYMONRRDS=BBRRDS
XYMONRSSTITLE=BBRSSTITLE
XYMONSERVERCGIURL=BBSERVERCGIURL
XYMONSERVERHOSTNAME=BBSERVERHOSTNAME
XYMONSERVERIP=BBSERVERIP
XYMONSERVERLOGS=BBSERVERLOGS
XYMONSERVEROS=BBSERVEROS
XYMONSERVERROOT=BBSERVERROOT
XYMONSERVERSECURECGIURL=BBSERVERSECURECGIURL
XYMONSERVERWWWNAME=BBSERVERWWWNAME
XYMONSERVERWWWURL=BBSERVERWWWURL
XYMONSKIN=BBSKIN
SLEEPBETWEENMSGS=BBSLEEPBETWEENMSGS
XYMONSNAPURL=BBSNAPURL
XYMONSNAPDIR=BBSNAP
XYMONTMP=BBTMP
XYMONVAR=BBVAR
XYMONWAP=BBWAP
XYMONWEBHOSTURL=BBWEBHOSTURL
XYMONWEBHOST=BBWEBHOST
XYMONWEBHTMLLOGS=BBWEBHTMLLOGS
XYMONWEB=BBWEB
XYMONWWWDIR=BBWWW
XYMONPAGEACKFONT=MKBBACKFONT
XYMONPAGECOLFONT=MKBBCOLFONT
XYMONPAGELOCAL=MKBBLOCAL
XYMONPAGEREMOTE=MKBBREMOTE
XYMONPAGEROWFONT=MKBBROWFONT
XYMONPAGESUBLOCAL=MKBBSUBLOCAL
XYMONPAGETITLE=MKBBTITLE
EOF1
	ln -s xymonserver.cfg hobbitserver.cfg
fi

cd $BBHOME || (echo "Cannot cd to BBHOME directory"; exit 1)
if test ! -w .; then echo "Cannot write to BBHOME directory"; exit 1; fi
renameandlink hobbit.sh xymon.sh


cd $BBHOME/bin || (echo "Cannot cd to bin directory"; exit 1)
if test ! -w .; then echo "Cannot write to bin directory"; exit 1; fi
renameandlink hobbit.sh xymon.sh
renameandlink bb xymon
renameandlink bbgen xymongen
renameandlink bbcmd xymoncmd
renameandlink bbhostgrep xymongrep
renameandlink bbhostshow xymoncfg
renameandlink hobbitlaunch xymonlaunch
renameandlink bbdigest xymondigest
renameandlink hobbitd xymond
renameandlink hobbitd_alert xymond_alert
renameandlink hobbitd_capture xymond_capture
renameandlink hobbitd_channel xymond_channel
renameandlink hobbitd_client xymond_client
renameandlink hobbitd_filestore xymond_filestore
renameandlink hobbitd_history xymond_history
renameandlink hobbitd_hostdata xymond_hostdata
renameandlink hobbitd_locator xymond_locator
renameandlink hobbitd_rrd xymond_rrd
renameandlink hobbitd_sample xymond_sample
renameandlink hobbitfetch xymonfetch
renameandlink hobbit-mailack xymon-mailack
renameandlink bbcombotest combostatus
renameandlink bbtest-net xymonnet
renameandlink bbretest-net.sh xymonnet-again.sh
renameandlink hobbitping xymonping
renameandlink hobbit-snmpcollect xymon-snmpcollect
renameandlink bbproxy xymonproxy
renameandlink bb-ack.cgi ack.cgi
renameandlink bb-csvinfo.cgi csvinfo.cgi
renameandlink bb-datepage.cgi datepage.cgi
renameandlink bb-eventlog.cgi eventlog.cgi
renameandlink bb-findhost.cgi findhost.cgi
renameandlink bb-hist.cgi history.cgi
renameandlink bb-histlog.cgi historylog.cgi
renameandlink bb-hostsvc.sh svcstatus.sh
renameandlink bb-message.cgi xymoncgimsg.cgi
renameandlink bb-rep.cgi report.cgi
renameandlink bb-replog.cgi reportlog.cgi
renameandlink bb-snapshot.cgi snapshot.cgi
renameandlink bb-webpage xymonpage
renameandlink hobbit-ackinfo.cgi ackinfo.cgi
renameandlink hobbit-certreport.sh certreport.sh
renameandlink hobbit-confreport.cgi confreport.cgi
renameandlink hobbit-enadis.cgi enadis.cgi
renameandlink hobbit-ghosts.cgi ghostlist.cgi
renameandlink hobbit-hostgraphs.cgi hostgraphs.cgi
renameandlink hobbit-hostlist.cgi hostlist.cgi
renameandlink hobbit-nkedit.cgi criticaleditor.cgi
renameandlink hobbit-nkview.cgi criticalview.cgi
renameandlink hobbit-nongreen.sh nongreen.sh
renameandlink hobbit-notifylog.cgi notifications.cgi
renameandlink hobbit-perfdata.cgi perfdata.cgi
renameandlink hobbit-statusreport.cgi statusreport.cgi
renameandlink hobbit-topchanges.sh topchanges.sh
renameandlink hobbit-useradm.cgi useradm.cgi
renameandlink hobbitcolumn.sh columndoc.sh
renameandlink hobbitgraph.cgi showgraph.cgi
renameandlink hobbitsvc.cgi svcstatus.cgi
renameandlink hobbitreports.sh xymonreports.sh

cd $BBHOME/web || (echo "Cannot cd to web-templates directory"; exit 1)
if test ! -w .; then echo "Cannot write to web-templates directory"; exit 1; fi
renameandlink bb_header stdnormal_header
renameandlink bb_footer stdnormal_footer
renameandlink bb2_header stdnongreen_header
renameandlink bb2_footer stdnongreen_footer
renameandlink bbnk_header stdcritical_header
renameandlink bbnk_footer stdcritical_footer
renameandlink bbsnap_header snapnormal_header
renameandlink bbsnap_footer snapnormal_footer
renameandlink bbsnap2_header snapnongreen_header
renameandlink bbsnap2_footer snapnongreen_footer
renameandlink bbsnapnk_header snapcritical_header
renameandlink bbsnapnk_footer snapcritical_footer
renameandlink bbrep_header repnormal_header
renameandlink bbrep_footer repnormal_footer
renameandlink hobbitnk_header critical_header
renameandlink hobbitnk_footer critical_footer
renameandlink nkack_header critack_header
renameandlink nkack_form critack_form
renameandlink nkack_footer critack_footer
renameandlink nkedit_header critedit_header
renameandlink nkedit_form critedit_form
renameandlink nkedit_footer critedit_footer

cd $BBHOME/ext || (echo "Cannot cd to ext directory"; exit 1)
if test ! -w .; then echo "Cannot write to ext directory"; exit 1; fi
renameandlink bbretest-net.sh xymonnet-again.sh


cd $BBSERVERROOT/cgi-bin || (echo "Cannot cd to cgi-bin directory"; exit 1)
if test ! -w .; then echo "Cannot write to cgi-bin directory"; exit 1; fi
renameandlink bb-hist.sh history.sh
renameandlink bb-eventlog.sh eventlog.sh
renameandlink bb-rep.sh report.sh
renameandlink bb-replog.sh reportlog.sh
renameandlink bb-snapshot.sh snapshot.sh
renameandlink bb-findhost.sh findhost.sh
renameandlink bb-csvinfo.sh csvinfo.sh
renameandlink hobbitcolumn.sh columndoc.sh
renameandlink bb-datepage.sh datepage.sh
renameandlink hobbitgraph.sh showgraph.sh
renameandlink bb-hostsvc.sh svcstatus.sh
renameandlink bb-histlog.sh historylog.sh
renameandlink hobbit-confreport.sh confreport.sh
renameandlink hobbit-confreport-critical.sh confreport-critical.sh
renameandlink hobbit-nkview.sh criticalview.sh
renameandlink hobbit-certreport.sh certreport.sh
renameandlink hobbit-nongreen.sh nongreen.sh
renameandlink hobbit-hostgraphs.sh hostgraphs.sh
renameandlink hobbit-ghosts.sh ghostlist.sh
renameandlink hobbit-notifylog.sh notifications.sh
renameandlink hobbit-hostlist.sh hostlist.sh
renameandlink hobbit-perfdata.sh perfdata.sh
renameandlink hobbit-topchanges.sh topchanges.sh

cd $BBSERVERROOT/cgi-secure || (echo "Cannot cd to cgi-secure directory"; exit 1)
if test ! -w .; then echo "Cannot write to cgi-secure directory"; exit 1; fi
renameandlink bb-ack.sh acknowledge.sh
renameandlink hobbit-enadis.sh enadis.sh
renameandlink hobbit-nkedit.sh criticaleditor.sh
renameandlink hobbit-ackinfo.sh ackinfo.sh
renameandlink hobbit-useradm.sh useradm.sh


cd $BBSERVERROOT/client/etc || (echo "Cannot cd to client/etc directory"; exit 1)
if test ! -w .; then echo "Cannot write to client/etc directory"; exit 1; fi
if test -f hobbitclient.cfg
then
	mv hobbitclient.cfg xymonclient.old
	$SDIRFULL/renamevars >xymonclient.cfg <<EOF2
XYMSRV=BBDISP
XYMSERVERS=BBDISPLAYS
XYMONDPORT=BBPORT
XYMONHOME=HOBBITCLIENTHOME
XYMON=BB
XYMONTMP=BBTMP
XYMONCLIENTLOGS=BBCLIENTLOGS
EOF2
	ln -s xymonclient.cfg hobbitclient.cfg
fi

cd $BBSERVERROOT/client/bin || (echo "Cannot cd to client/bin directory"; exit 1)
if test ! -w .; then echo "Cannot write to client/bin directory"; exit 1; fi
renameandlink bb xymon
renameandlink bbcmd xymoncmd
renameandlink bbdigest xymondigest
renameandlink bbhostgrep xymongrep
renameandlink bbhostshow xymoncfg
renameandlink hobbitclient-aix.sh xymonclient-aix.sh
renameandlink hobbitclient-darwin.sh xymonclient-darwin.sh
renameandlink hobbitclient-freebsd.sh xymonclient-freebsd.sh
renameandlink hobbitclient-hp-ux.sh xymonclient-hp-ux.sh
renameandlink hobbitclient-irix.sh xymonclient-irix.sh
renameandlink hobbitclient-linux.sh xymonclient-linux.sh
renameandlink hobbitclient-netbsd.sh xymonclient-netbsd.sh
renameandlink hobbitclient-openbsd.sh xymonclient-openbsd.sh
renameandlink hobbitclient-osf1.sh xymonclient-osf1.sh
renameandlink hobbitclient-sco_sv.sh xymonclient-sco_sv.sh
renameandlink hobbitclient.sh xymonclient.sh
renameandlink hobbitclient-sunos.sh xymonclient-sunos.sh
renameandlink hobbitlaunch xymonlaunch
renameandlink orcahobbit orcaxymon


cd $BBRRDS/$MACHINEDOTS || (echo "Cannot cd to Xymon-servers\' RRD directory"; exit 1)
if test ! -w .; then echo "Cannot write to Xymon-servers\' RRD directory"; exit 1; fi
if test -f hobbitd.rrd; then mv hobbitd.rrd xymond.rrd; fi
if test -f bbgen.rrd; then mv bbgen.rrd xymongen.rrd; fi
if test -f bbtest.rrd; then mv bbtest.rrd xymonnet.rrd; fi
if test -f bbproxy.rrd; then mv bbproxy.rrd xymonproxy.rrd; fi

cd $BBHIST || (echo "Cannot cd to Xymon-servers\' history directory"; exit 1)
if test ! -w .; then echo "Cannot write to Xymon-servers\' history directory"; exit 1; fi
if test -f $MACHINE.bbgen; then mv $MACHINE.bbgen $MACHINE.xymongen; fi
if test -f $MACHINE.bbtest; then mv $MACHINE.bbtest $MACHINE.xymonnet; fi
if test -f $MACHINE.hobbitd; then mv $MACHINE.hobbitd $MACHINE.xymond; fi
if test -f $MACHINE.bbproxy; then mv $MACHINE.bbproxy $MACHINE.xymonproxy; fi

MACHINEUS=`echo $MACHINE | sed -e 's!,!_!g'`
cd $BBHISTLOGS/$MACHINEUS || (echo "Cannot cd to Xymon-servers\' histlogs directory"; exit 1)
if test ! -w .; then echo "Cannot write to Xymon-servers\' histlogs directory"; exit 1; fi
if test -d bbgen; then mv bbgen xymongen; fi
if test -d bbtest; then mv bbtest xymonnet; fi
if test -d hobbitd; then mv hobbitd xymond; fi
if test -d bbproxy; then mv bbproxy xymonproxy; fi

cd $BBWWW || (echo "Cannot cd to Xymon-servers\' www directory"; exit 1)
if test ! -w .; then echo "Cannot write to Xymon-servers\' www directory"; exit 1; fi
renameandlink bb.html xymon.html
renameandlink bb2.html nongreen.html
renameandlink bbnk.html critical.html


echo ""
echo "Things you must do"
echo "=================="
echo ""
echo "*  Run 'make install' now to install Xymon 4.3.0."
echo ""
echo "*  If you have modified web templates files, then these have"
echo "   NOT been updated with the new default templates. You must"
echo "   manually install the new templates from the xymond/webfiles"
echo "   directory, and copy over any changes you have made locally."
echo "   All of the template files were updated to include a link"
echo "   to the new xymon.sourceforge.net website, the DOCTYPE header"
echo "   was updated from '4.0 Transitional' to '4.0', the Java"
echo "   menu-code in the footer-files was replaced with &XYMONBODYFOOTER"
echo "   the menu stylesheet was changed to &XYMONBODYMENUCSS, and a line"
echo "   with &XYMONBODYHEADER was added just after the BODY tag in all"
echo "   header-files."
echo "   The following files had further changes from 4.2.3 to 4.3.0:"
echo "     - event_form"
echo "     - hostgraphs_form"
echo "     - notify_form"
echo "   If you modified these three files, it is probably best to"
echo "   copy the new standard files from xymond/webfiles/ to your"
echo "   $BBHOME/web/ and re-add your local changes."
echo ""
echo "*  If you have modified the menu-entries in the Xymon menu, then"
echo "   these changes have not been ported over to the new setup. You"
echo "   must add these changes to the new $BBHOME/etc/xymonmenu.cfg file."
echo ""
echo "*  In your Apache (webserver) configuration, add"
echo "   FollowSymLinks to the Options line for the"
echo "   $BBSERVERROOT/cgi-bin and $BBSERVERROOT/cgi-secure directories."
echo ""
echo "*  Also in your Apache (webserver) configuration, add"
echo "   Rewrite-rules so the old URL's for CGI-scripts and webpages"
echo "   still work. See the xymond/etcfiles/xymon-apache.conf example file."
echo ""

exit 0

