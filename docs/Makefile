all: xymon-tips.html xymon-apacheconf.txt install.html

xymon-tips.html: xymon-tips.html.DIST
	cat $< | sed -e 's!@XYMONHOSTURL@!$(XYMONHOSTURL)!g' >$@

xymon-apacheconf.txt: xymon-apacheconf.txt.DIST
	cat $< \
	  | sed -e 's!@INSTALLWWWDIR@!$(INSTALLWWWDIR)!g' \
	  | sed -e 's!@CGIDIR@!$(CGIDIR)!g' \
	  | sed -e 's!@SECURECGIDIR@!$(SECURECGIDIR)!g' \
	  | sed -e 's!@INSTALLETCDIR@!$(INSTALLETCDIR)!g' >$@

install.html: install.html.DIST
	cat $< \
	  | sed -e 's!@INSTALLETCDIR@!$(INSTALLETCDIR)!g' \
	  | sed -e 's!@XYMONTOPDIR@!$(XYMONTOPDIR)!g' \
	  | sed -e 's!@XYMONUSER@!$(XYMONUSER)!g' >$@

clean:
	rm -f xymon-tips.html xymon-apacheconf.txt install.html *~

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
