# NAME
#       Makefile 
#
# REVISION HISTORY
#     08/04 2008 T.J. Yang Init
#     08/26 2008 T.J. Yang Add slides in text format  for content checking.
# 
# USAGE
#       make
#
# DESCRIPTION
#       This GNU makefile script will call up pdflatex to generate pdf file using beamer 
#       latex class.
#       Slides:
#        1. xymon-slides-developer.pdf 
#        2. xymon-slides-sysadmin.pdf 
#        3. xymon-slides-intro.pdf 
#       It will upload the tex and generated pdf files to xymonmon sourceforge.net site.
#
# ---------------------------- CONSTANT DECLARATION ---------------------------
#
# WHAT: Define exit status flags.
# WHY:  Easier to program with a mnemonic than with the numbers.
# 
#

SUCCESS:=0
ERROR:=1
WARNING:=2
PDFLATEX:=/usr/bin/pdflatex
PS2TXT:=/usr/bin/ps2ascii
## ---------------------------------- SCRIPT -----------------------------------
##

# WHAT: make slides and check in the modified files.
#all: pdf svncheckin

# text file for prework 
all: pdf



# WHAT: slides in text format
txt: pdf xymon-slides-developer.txt  xymon-slides-intro.txt \
        xymon-slides-sysadmin.txt


# WHAT: slides in pdf format
pdf: xymon-slides-developer.pdf  xymon-slides-intro.pdf \
        xymon-slides-sysadmin.pdf

# WHAT: Hobbit Introduction Slides
xymon-slides-intro.pdf: xymon-slides-intro.tex
	${PDFLATEX}  xymon-slides-intro.tex


# WHAT: 
xymon-slides-intro.txt: xymon-slides-intro.pdf
	${PS2TXT}  xymon-slides-intro.pdf  xymon-slides-intro.txt


# WHAT: 
xymon-slides-developer.txt: xymon-slides-developer.pdf
	${PS2TXT}  xymon-slides-developer.pdf  xymon-slides-developer.txt

# WHAT: 
xymon-slides-sysadmin.txt: xymon-slides-sysadmin.pdf
	${PS2TXT}  xymon-slides-sysadmin.pdf  xymon-slides-sysadmin.txt


# WHAT: Hobbit Slides for Developer
xymon-slides-developer.pdf: xymon-slides-developer.tex
	${PDFLATEX}  xymon-slides-developer.tex

# WHAT: Hobbit Slides for System Administrators
xymon-slides-sysadmin.pdf: xymon-slides-sysadmin.tex
	${PDFLATEX}  xymon-slides-sysadmin.tex

# WHAT: clean up all the unwanted files.
clean:
	rm -rf 	*.{aux,log,nav,out,snm,toc,vrb,ps,txt,pdf} *~

# WHAT: use svn to checkin the changes and updated pdf file.
svncheckin: all
	svn commit -m "automatic checkin, use ViewVC to see details."

# WHAT: clean out the unwanted files first  
#       and use "svn status" command  to display files are not committed.
svnstatus: clean
	svn status
