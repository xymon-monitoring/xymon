WEBLIST="webfiles/acknowledge_form webfiles/acknowledge_header webfiles/bb2_header webfiles/bb_footer webfiles/bb_header webfiles/bbnk_header webfiles/bbrep_header webfiles/bbsnap_header webfiles/columndoc_header webfiles/event_form webfiles/event_header webfiles/findhost_form webfiles/findhost_header webfiles/graphs_header webfiles/hist_header webfiles/histlog_header webfiles/hostsvc_header webfiles/info_header webfiles/maint_header webfiles/replog_header webfiles/report_form webfiles/report_header webfiles/snapshot_form webfiles/snapshot_header webfiles/zoom.js"
WWWLIST="wwwfiles/gifs/clear.gif wwwfiles/gifs/green.gif wwwfiles/gifs/yellow.gif wwwfiles/gifs/red.gif wwwfiles/gifs/unknown-recent.gif wwwfiles/gifs/bkg-clear.gif wwwfiles/gifs/bkg-green.gif wwwfiles/gifs/README wwwfiles/gifs/purple.gif wwwfiles/gifs/bkg-blue.gif wwwfiles/gifs/zoom.gif wwwfiles/gifs/blue-recent.gif wwwfiles/gifs/red-recent.gif wwwfiles/gifs/blue.gif wwwfiles/gifs/yellow-ack.gif wwwfiles/gifs/purple-recent.gif wwwfiles/gifs/green-recent.gif wwwfiles/gifs/unknown.gif wwwfiles/gifs/arrow.gif wwwfiles/gifs/purple-ack.gif wwwfiles/gifs/red-ack.gif wwwfiles/gifs/yellow-recent.gif wwwfiles/gifs/bkg-red.gif wwwfiles/gifs/bkg-yellow.gif wwwfiles/gifs/clear-recent.gif wwwfiles/gifs/bkg-purple.gif wwwfiles/menu/menu.css wwwfiles/menu/README wwwfiles/menu/menu_tpl.js wwwfiles/menu/menu.js wwwfiles/menu/menu_items.js.DIST"

VERLIST="4.0-beta4 4.0-beta5 4.0-beta6 4.0-RC1 4.0-RC2"

for F in $WEBLIST $WWWLIST
do
	for V in $VERLIST
	do
		if test -f hobbit-$V/hobbitd/$F; then
			echo "`../common/bbdigest md5 hobbit-$V/hobbitd/$F` $F"
		fi
	done
done | sort -k2 | uniq

