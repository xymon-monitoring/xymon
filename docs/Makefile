all:
	cat xymon-tips.html.DIST | sed -e 's!@XYMONHOSTURL@!$(XYMONHOSTURL)!g' >xymon-tips.html

clean:
	rm -f xymon-tips.html *~

install:
	mkdir -p                             $(INSTALLROOT)$(INSTALLSTATICWWWDIR)/help/manpages
	cd manpages; tar cf - . | (cd        $(INSTALLROOT)$(INSTALLSTATICWWWDIR)/help/manpages; tar xf -)
	cp -f *html *txt *png *jpg           $(INSTALLROOT)$(INSTALLSTATICWWWDIR)/help/; rm -f $(INSTALLROOT)$(INSTALLSTATICWWWDIR)/help/man-index.html
	cp -f man-index.html                 $(INSTALLROOT)$(INSTALLSTATICWWWDIR)/help/manpages/index.html
ifndef PKGBUILD
	chown -R $(XYMONUSER)                $(INSTALLROOT)$(INSTALLSTATICWWWDIR)/help
	chgrp -R `$(IDTOOL) -g $(XYMONUSER)` $(INSTALLROOT)$(INSTALLSTATICWWWDIR)/help
	# These may fail if no files installed
	(find                                $(INSTALLROOT)$(INSTALLSTATICWWWDIR)/help/ -type d | xargs chmod 755 ) || /bin/true
	(find                                $(INSTALLROOT)$(INSTALLSTATICWWWDIR)/help/ -type f | xargs chmod 644 ) || /bin/true
endif

