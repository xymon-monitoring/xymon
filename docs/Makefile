all:
	@echo ""

clean:
	rm -f *~

install:
	mkdir -p $(BBHOME)/www/help/manpages
	cd manpages; tar cf - . | (cd $(BBHOME)/www/help/manpages; tar xf -)
	cp -f *html *txt *png $(BBHOME)/www/help/; rm -f $(BBHOME)/www/help/man-index.html
	cp -f man-index.html $(BBHOME)/www/help/manpages/index.html

