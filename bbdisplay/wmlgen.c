/*----------------------------------------------------------------------------*/
/* Big Brother webpage generator tool.                                        */
/*                                                                            */
/* This is a replacement for the "mkbb.sh" and "mkbb2.sh" scripts from the    */
/* "Big Brother" monitoring tool from BB4 Technologies.                       */
/*                                                                            */
/* Primary reason for doing this: Shell scripts perform badly, and with a     */
/* medium-sized installation (~150 hosts) it takes several minutes to         */
/* generate the webpages. This is a problem, when the pages are used for      */
/* 24x7 monitoring of the system status.                                      */
/*                                                                            */
/* Copyright (C) 2002 Henrik Storner <henrik@storner.dk>                      */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: wmlgen.c,v 1.9 2003-05-22 22:34:22 henrik Exp $";

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>

#include "bbgen.h"
#include "util.h"
#include "wmlgen.h"

static char wmldir[MAX_PATH];

static void delete_old_cards(char *dirname)
{
	DIR             *bbcards;
	struct dirent   *d;
	struct stat     st;
	time_t		now = time(NULL);
	char		fn[MAX_PATH];

	bbcards = opendir(dirname);
	if (!bbcards) {
		errprintf("Cannot read directory %s\n", dirname);
		return;
        }

	chdir(dirname);
	while ((d = readdir(bbcards))) {
		strcpy(fn, d->d_name);
		stat(fn, &st);
		if (S_ISREG(st.st_mode) && (st.st_mtime < (now-3600))) {
			unlink(fn);
		}
	}
	closedir(bbcards);
}

static char *wml_colorname(int color)
{
	switch (color) {
	  case COL_GREEN:  return "GR"; break;
	  case COL_RED:    return "RE"; break;
	  case COL_YELLOW: return "YE"; break;
	  case COL_PURPLE: return "PU"; break;
	  case COL_CLEAR:  return "CL"; break;
	  case COL_BLUE:   return "BL"; break;
	}

	return "";
}

static void generate_wml_statuscard(host_t *host, entry_t *entry)
{
	char fn[MAX_PATH];
	FILE *fd;
	char logfn[MAX_PATH];
	FILE *logfd;
	char l[MAX_LINE_LEN], lineout[MAX_LINE_LEN];
	char *p, *outp;
	int linecount;

	sprintf(fn, "%s/%s.%s.wml", wmldir, host->hostname, entry->column->name);
	fd = fopen(fn, "w");
	if (fd == NULL) {
		errprintf("Cannot open file %s\n", fn);
		return;
	}

	sprintf(logfn, "%s/%s.%s", getenv("BBLOGS"), commafy(host->hostname), entry->column->name);
	logfd = fopen(logfn, "r");
	if (logfd == NULL) {
		fclose(fd);
		errprintf("Cannot open file %s\n", logfn);
		return;
	}

	fprintf(fd, "<?xml version=\"1.0\"?>\n");
	fprintf(fd, "<!DOCTYPE wml PUBLIC \"-//WAPFORUM//DTD WML 1.1//EN\" \"http://www.wapforum.org/DTD/wml_1.1.xml\">\n");
	fprintf(fd, "<wml>\n");
	fprintf(fd, "<head>\n");
	fprintf(fd, "<meta http-equiv=\"Cache-Control\" content=\"max-age=0\"/>\n");
	fprintf(fd, "</head>\n");
	fprintf(fd, "<card id=\"name1\" title=\"BigBrother\">\n");
	fprintf(fd, "<p align=\"center\">\n");
	fprintf(fd, "<anchor title=\"BB\">Host<go href=\"%s.wml\"/></anchor><br/>\n", host->hostname);
	fprintf(fd, "%s</p>\n", timestamp);
	fprintf(fd, "<p align=\"left\" mode=\"nowrap\">\n");
	fprintf(fd, "<b>%s.%s</b><br/></p><p mode=\"nowrap\">\n", host->hostname, entry->column->name);

	linecount = 0;
	while ( (linecount < 14) && (fgets(l, sizeof(l), logfd))) {
		outp = lineout;

		p = strchr(l, '\n'); if (p) *p = '\0';
		for (p=l; (*p && isspace((int) *p)); p++) ;

		if (strlen(p) == 0) {
			/* Empty line - ignore */
		}
		else if (strstr(l, "DOCTYPE")) {
			/* DOCTYPE - ignore */
		}
		else {
			for (p=l; (*p); ) {
				if (strncmp(p, "http://", 7) == 0) {
					p += 7;
				}
				else if (strncasecmp(p, "<tr>", 4) == 0) {
					strcpy(outp, "<br/>");
					outp += 5;
					p += 4;
				}
				else if (*p == '<') {
					strcpy(outp, "&lt;");
					outp += 4; p++;
				}
				else if (*p == '>') {
					strcpy(outp, "&gt;");
					outp += 4; p++;
				}
				else if (strncmp(p, "&red", 4) == 0) {
					strcpy(outp, "<b>red</b>");
					outp += 10; p += 4;
				}
				else if (strncmp(p, "&green", 6) == 0) {
					strcpy(outp, "<b>green</b>");
					outp += 12; p += 6;
				}
				else if (strncmp(p, "&purple", 7) == 0) {
					strcpy(outp, "<b>purple</b>");
					outp += 13; p += 7;
				}
				else if (strncmp(p, "&yellow", 7) == 0) {
					strcpy(outp, "<b>yellow</b>");
					outp += 13; p += 7;
				}
				else if (strncmp(p, "&clear", 6) == 0) {
					strcpy(outp, "<b>clear</b>");
					outp += 12; p += 6;
				}
				else if (strncmp(p, "&blue", 5) == 0) {
					strcpy(outp, "<b>blue</b>");
					outp += 11; p += 5;
				}
				else if (*p == '&') {
					strcpy(outp, "&amp;");
					outp += 5; p++;
				}
				else if (*p == '\'') {
					strcpy(outp, "&apos;");
					outp += 6; p++;
				}
				else if (*p == '\"') {
					strcpy(outp, "&quot;");
					outp += 6; p++;
				}
				else {
					*outp = *p;
					outp++; p++; 
				}
			}
		}
		*outp = '\0';
		if (strlen(lineout)) fprintf(fd, "%s\n<br/>\n", lineout);
	}
	fprintf(fd, "<br/> </p> </card> </wml>\n");

	fclose(logfd);
	fclose(fd);
}


int do_wml_cards(char *webdir)
{
	FILE		*bb2fd, *hostfd;
	char		bb2fn[MAX_PATH], indexfn[MAX_PATH], hostfn[MAX_PATH];
	hostlist_t	*h;
	entry_t		*t;
	int		wapcolor, hostcolor;
	long wmlmaxchars = atol(getenv("WMLMAXCHARS"));
	int bb2part = 1;

	sprintf(wmldir, "%s/wml", webdir);
	delete_old_cards(wmldir);

	wapcolor = COL_GREEN;
	for (h = hosthead; (h); h = h->next) {
		hostcolor = COL_GREEN;
		for (t = h->hostentry->entries; (t); t = t->next) {
			if (t->onwap && ((t->color == COL_RED) || (t->color == COL_YELLOW))) 
				generate_wml_statuscard(h->hostentry, t);

			if (t->onwap && (t->color > hostcolor)) hostcolor = t->color;
		}

		/* We only care about RED or YELLOW */
		switch (hostcolor) {
		 case COL_RED:
		 case COL_YELLOW:
			if (hostcolor > wapcolor) wapcolor = hostcolor;
			h->hostentry->anywaps = 1;
		}
	}

	/* Start the BB2 WML card */
	sprintf(bb2fn, "%s/bb2.wml", wmldir);
	bb2fd = fopen(bb2fn, "w");
	if (bb2fd == NULL) {
		errprintf("Cannot open BB2 WML file %s\n", bb2fn);
		return 0;
	}

	sprintf(indexfn, "%s/index.wml", wmldir);
	symlink(bb2fn, indexfn);

	fprintf(bb2fd, "<?xml version=\"1.0\"?>\n");
	fprintf(bb2fd, "<!DOCTYPE wml PUBLIC \"-//WAPFORUM//DTD WML 1.1//EN\" \"http://www.wapforum.org/DTD/wml_1.1.xml\">\n");
	fprintf(bb2fd, "<wml>\n");
	fprintf(bb2fd, "<head>\n");
	fprintf(bb2fd, "<meta http-equiv=\"Cache-Control\" content=\"max-age=0\"/>\n");
	fprintf(bb2fd, "</head>\n");
	fprintf(bb2fd, "<card id=\"card%d\" title=\"BigBrother\">\n", bb2part);
	fprintf(bb2fd, "<p align=\"center\" mode=\"nowrap\">\n");
	fprintf(bb2fd, "%s</p>\n", timestamp);
	fprintf(bb2fd, "<p align=\"center\" mode=\"nowrap\">\n");
	fprintf(bb2fd, "Summary Status<br/><b>%s</b><br/><br/>\n", colorname(wapcolor));

	for (h = hosthead; (h); h = h->next) {
		if (h->hostentry->anywaps) {
			hostcolor = COL_GREEN;

			sprintf(hostfn, "%s/%s.wml", wmldir, h->hostentry->hostname);
			hostfd = fopen(hostfn, "w");
			if (hostfd == NULL) {
				errprintf("Cannot create file %s\n", hostfn);
				return 0;
			}

			fprintf(hostfd, "<?xml version=\"1.0\"?>\n");
			fprintf(hostfd, "<!DOCTYPE wml PUBLIC \"-//WAPFORUM//DTD WML 1.1//EN\" \"http://www.wapforum.org/DTD/wml_1.1.xml\">\n");
			fprintf(hostfd, "<wml>\n");
			fprintf(hostfd, "<head>\n");
			fprintf(hostfd, "<meta http-equiv=\"Cache-Control\" content=\"max-age=0\"/>\n");
			fprintf(hostfd, "</head>\n");
			fprintf(hostfd, "<card id=\"name1\" title=\"BigBrother\">\n");
			fprintf(hostfd, "<p align=\"center\">\n");
			fprintf(hostfd, "<anchor title=\"BB\">Overall<go href=\"bb2.wml\"/></anchor><br/>\n");
			fprintf(hostfd, "%s</p>\n", timestamp);
			fprintf(hostfd, "<p align=\"left\" mode=\"nowrap\">\n");
			fprintf(hostfd, "<b>%s</b><br/><br/>\n", h->hostentry->hostname);

			for (t = h->hostentry->entries; (t); t = t->next) {
				if (t->onwap && (t->color > hostcolor)) hostcolor = t->color;

				if (t->onwap && ((t->color == COL_RED) || (t->color == COL_YELLOW))) {
					fprintf(hostfd, "<b><anchor title=\"%s\">%s%s<go href=\"%s.%s.wml\"/></anchor></b> %s<br/>\n", 
						t->column->name, 
						wml_colorname(t->color),
						(t->acked ? "x" : ""),
						h->hostentry->hostname, t->column->name,
						t->column->name);
				}
			}
			fprintf(hostfd, "\n</p> </card> </wml>\n");
			fclose(hostfd);

			fprintf(bb2fd, "<b><anchor title=\"%s\">%s<go href=\"%s.wml\"/></anchor></b> %s<br/>\n", 
				h->hostentry->hostname, wml_colorname(hostcolor), h->hostentry->hostname, h->hostentry->hostname);

			if (ftell(bb2fd) >= wmlmaxchars) {
				char oldbb2fn[MAX_PATH];

				/* WML link is from the bb2fn except leading wmldir+'/' */
				strcpy(oldbb2fn, bb2fn+strlen(wmldir)+1);

				bb2part++;

				fprintf(bb2fd, "<br /><b><anchor title=\"Next\">Next<go href=\"bb2-%d.wml\"/></anchor></b>\n", bb2part);
				fprintf(bb2fd, "</p> </card> </wml>\n");
				fclose(bb2fd);

				/* Start a new BB2 WML card */
				sprintf(bb2fn, "%s/bb2-%d.wml", wmldir, bb2part);
				bb2fd = fopen(bb2fn, "w");
				if (bb2fd == NULL) {
					errprintf("Cannot open BB2 WML file %s\n", bb2fn);
					return 0;
				}
				fprintf(bb2fd, "<?xml version=\"1.0\"?>\n");
				fprintf(bb2fd, "<!DOCTYPE wml PUBLIC \"-//WAPFORUM//DTD WML 1.1//EN\" \"http://www.wapforum.org/DTD/wml_1.1.xml\">\n");
				fprintf(bb2fd, "<wml>\n");
				fprintf(bb2fd, "<head>\n");
				fprintf(bb2fd, "<meta http-equiv=\"Cache-Control\" content=\"max-age=0\"/>\n");
				fprintf(bb2fd, "</head>\n");
				fprintf(bb2fd, "<card id=\"card%d\" title=\"BigBrother\">\n", bb2part);
				fprintf(bb2fd, "<p align=\"center\">\n");
				fprintf(bb2fd, "<anchor title=\"Prev\">Previous<go href=\"%s\"/></anchor><br/>\n", oldbb2fn);
				fprintf(bb2fd, "%s</p>\n", timestamp);
				fprintf(bb2fd, "<p align=\"center\" mode=\"nowrap\">\n");
				fprintf(bb2fd, "Summary Status<br/><b>%s</b><br/><br/>\n", colorname(wapcolor));
			}
		}
	}

	if (wapcolor == COL_GREEN) {
		fprintf(bb2fd, "All is OK<br/>\n");
	}
	fprintf(bb2fd, "</p> </card> </wml>\n");
	fclose(bb2fd);

	return 1;
}

