# This makefile requires GNU make.
BOOKNAME= xymonmon-sysadmin-guide
sources := \
	00book.tex \
	99book.bib \
	99defs.tex \
	build_id.tex \
	preface.tex \
	intro.tex \
        xymon_overview.tex \
        web_page_generation.tex \
        server_configuration_files.tex \
        report_generation.tex \
        network_service_testing.tex \
        messages_protocols.tex \
        xymon_server_programs.tex \
        xymon_client.tex \
        data_backend.tex \
        combination_tests.tex \
	bookmaking.tex \
        administrative_web_pages.tex \
        howtograph.tex \
        install.tex \
        about.tex \
        xymon-alerts.tex \
        xymon-config.tex \
        bb-to-xymon.tex \
        xymon-mrtg.tex \
        criticalsystems.tex \
        known-issues.tex \
	license.tex

image-sources := \
	feature-branches.dot \
	filelog.svg \
	kdiff3.png \
	metadata.svg \
	mq-stack.svg \
	note.png \
	revlog.svg \
	snapshot.svg \
	tour-history.svg \
	tour-merge-conflict.svg \
	tour-merge-merge.svg \
	tour-merge-pull.svg \
	tour-merge-sep-repos.svg \
	undo-manual.dot \
	undo-manual-merge.dot \
	undo-non-tip.dot \
	undo-simple.dot \
	wdir.svg \
	wdir-after-commit.svg \
	wdir-branch.svg \
	wdir-merge.svg \
	wdir-pre-branch.svg

# filter
image-dot := $(filter %.dot,$(image-sources))
image-svg := $(filter %.svg,$(image-sources))
image-png := $(filter %.png,$(image-sources))

image-pdf := $(image-dot:%.dot=%.pdf) $(image-svg:%.svg=%.pdf) $(image-png)
image-html := $(image-dot:%.dot=%.png) $(image-svg:%.svg=%.png) $(image-png)

example-sources := \
	backout \
	bisect \
	branching \
	branch-named \
	branch-repo \
	cmdref \
	daily.copy \
	daily.files \
	daily.rename \
	daily.revert \
	extdiff \
	filenames \
	hook.msglen \
	hook.simple \
	hook.ws \
	issue29 \
	mq.guards \
	mq.qinit-help \
	mq.dodiff \
	mq.id \
	mq.tarball \
	mq.tools \
	mq.tutorial \
	rename.divergent \
	rollback \
	tag \
	template.simple \
	template.svnstyle \
	tour \
	tour-merge-conflict

example-prereqs := \
	/usr/bin/merge

dist-sources := \
	../html/hgicon.png \
	../html/index.html.var \
	../html/index.en.html

latex-options = \
	-interaction batchmode \
	-output-directory $(dir $(1)) \
	-jobname $(basename $(notdir $(1)))

hg = $(shell which hg)

hg-id = $(shell hg parents --template '{node|short}, dated {date|isodate},\n')

hg-version = $(shell hg version -q | \
	     sed 's,.*(version \(unknown\|[a-f0-9+]*\)),\1,')

all: pdf 
#all: html 

pdf: pdf/${BOOKNAME}.pdf

# What: A makefile macro function to 
# $< :
# $@ :
define pdf
	mkdir -p $(dir $@)
	TEXINPUTS=$(dir $<): pdflatex $(call latex-options,$@) $< || (rm -f $@; exit 1)
	cp 99book.bib $(dir $@)
	cd $(dir $@) && bibtex $(basename $(notdir $@))
	cd $(dir $@) && makeindex $(basename $(notdir $@))
	TEXINPUTS=$(dir $<): pdflatex $(call latex-options,$@) $< || (rm -f $@; exit 1)
	TEXINPUTS=$(dir $<): pdflatex $(call latex-options,$@) $< || (rm -f $@; exit 1)
	if grep 'Reference.*undefined' $(@:.pdf=.log); then exit 1; fi
endef

#pdf/${BOOKNAME}.pdf: $(sources) $(image-pdf) examples
pdf/${BOOKNAME}.pdf: $(sources) 
	$(call pdf)


html: onepage split

onepage: $(htlatex) html/onepage/hgbook.html html/onepage/hgbook.css $(image-html:%=html/onepage/%)

html/onepage/%: %
	cp $< $@

split: $(htlatex) html/split/hgbook.html html/split/hgbook.css $(image-html:%=html/split/%)

html/split/%: %
	cp $< $@

# This is a horrible hack to work around the fact that the htlatex
# command in tex4ht is itself a horrible hack.  I really don't want to
# include verbatim the big wad of TeX that is repeated in that script,
# but I've given up and run a hacked copy as htlatex.book here.

define htlatex
	mkdir -p $(dir $(1))
	cp 99book.bib $(dir $(1))
	TEXINPUTS=$(dir $(2)): ./htlatex.book $(2) "bookhtml,html4-uni,$(3)" " -cunihtf -utf8" "$(dir $(1))" "$(call latex-options,$(1))" || (rm -f $(1); exit 1)
	cd $(dir $(1)) && tex4ht -f/$(basename $(notdir $(1))) -cvalidate -cunihtf
	cd $(dir $(1)) && t4ht -f/$(basename $(notdir $(1)))
	./fixhtml.py $(dir $(1))/*.html
	rm $(dir $(1))/hgbook.css
endef

html/onepage/hgbook.html: $(sources) $(image-html) examples bookhtml.cfg
	$(call htlatex,$@,$<)

html/split/hgbook.html: $(sources) examples bookhtml.cfg
	$(call htlatex,$@,$<,2)

%.tex: %.html
	html2latex $@ $<

# Produce 90dpi PNGs for the web.

%.png: %.svg
	inkscape -D -e $@ $<

%.svg: %.dot
	dot -Tsvg -o $@ $<

# Produce eps & pdf for the pdf

%.pdf: %.eps
	epstopdf $<

%.eps: %.svg
	inkscape -E $@ $<

%.eps: %.dot
	dot -Tps -o $@ $<

examples: $(example-prereqs) examples/.run

examples/.run: $(example-sources:%=examples/%.run)
	touch examples/.run

examples/%.run: examples/% examples/run-example
	cd examples && ./run-example $(notdir $<)

changelog := $(wildcard ../.hg/store/00changelog.[id])
ifeq ($(changelog),)
changelog := $(wildcard ../.hg/00changelog.[id])
endif

build_id.tex: $(changelog)
	echo -n '$(hg-id)' > build_id.tex

hg_id.tex: $(hg)
	echo -n '$(hg-version)' > hg_id.tex

clean: clean_slides
	rm -rf dist html pdf/*  \
		$(image-dot:%.dot=%.pdf) \
		$(image-dot:%.dot=%.png) \
		$(image-svg:%.svg=%.pdf) \
		$(image-svg:%.svg=%.png) \
		examples/*.{lxo,run} examples/.run build_id.tex hg_id.tex \
                *~ \
                }

clean_slides:
	rm -rf 	slides/*.{aux,log,nav,out,snm,toc,pdf}

install: pdf split $(dist-sources)
	rm -rf dist
	mkdir -p dist
	cp pdf/${BOOKNAME}.pdf dist
	cp html/split/*.{css,html,png} dist
	cp $(dist-sources) dist

rsync: install
	rsync -avz --delete dist sp.red-bean.com:public_html/hgbook

upload: all 
	sudo rsync -avz --delete pdf/*.pdf  /var/www/html/pdf
	sudo rsync -avz --delete html   /var/www/html/pdf/
tar: 
	tar czvf /tmp/xymon-rtfm.tgz  /home/tjyang/docs/hobbit-rtfm
	sudo rsync -avz --delete /tmp/*.tgz /var/www/html/pdf
svncheckin:
	svn commit -m "Auto commit by Makefile"


#perl -pi -e 's!\\hline!!' *.tex 
#perl -pi -e 's!\\\\!!' *.tex 
#perl -pi -e 's!\\begin\{document.*!!' *.tex 
#perl -pi -e 's!\\end\{doc.*!!' *.tex 
#perl -pi -e 's!\\title!\\chapter!' *.tex 
#perl -pi -e 's!\\section\*!\\section!' *.tex 
#perl -pi -e 's!\\subsection\*!\\subsection!' *.tex 
#perl -pi -e 's!\\documentclass.*!!' *.tex 
#perl -pi -e 's!\\usepackage.*!!' *.tex 
#perl -pi -e 's!\\setlength.*!!' *.tex 
#perl -pi -e 's!Man page of !!' *.tex 
#perl -pi -e 's!\\subsection!\\section!' *.tex 



