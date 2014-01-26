all:
	cat xymon-tips.html.DIST | sed -e 's!@XYMONHOSTURL@!$(XYMONHOSTURL)!g' >xymon-tips.html

clean:
	rm -f xymon-tips.html *~

install:
	mkdir -p $(INSTALLROOT)$(INSTALLWWWDIR)/help/manpages
	cd manpages; tar cf - . | (cd $(INSTALLROOT)$(INSTALLWWWDIR)/help/manpages; tar xf -)
	cp -f *html *txt *png *jpg $(INSTALLROOT)$(INSTALLWWWDIR)/help/; rm -f $(INSTALLROOT)$(INSTALLWWWDIR)/help/man-index.html
	cp -f man-index.html $(INSTALLROOT)$(INSTALLWWWDIR)/help/manpages/index.html
ifndef PKGBUILD
	chown -R $(XYMONUSER) $(INSTALLROOT)$(INSTALLWWWDIR)/help
	chgrp -R `$(IDTOOL) -g $(XYMONUSER)` $(INSTALLROOT)$(INSTALLWWWDIR)/help
	# These may fail if no files installed
	(find $(INSTALLROOT)$(INSTALLWWWDIR)/help/ -type d | xargs chmod 755 ) || /bin/true
	(find $(INSTALLROOT)$(INSTALLWWWDIR)/help/ -type f | xargs chmod 644 ) || /bin/true
endif

