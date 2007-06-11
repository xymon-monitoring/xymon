/*----------------------------------------------------------------------------*/
/* Hobbit WML generator.                                                      */
/*                                                                            */
/* This file contains code to generate the WAP/WML documents showing the      */
/* Hobbit status.                                                             */
/*                                                                            */
/* Copyright (C) 2002-2006 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: wmlgen.c,v 1.27 2007-06-11 14:40:13 henrik Exp $";

#include <limits.h>
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
#include "wmlgen.h"
#include "util.h"

int enable_wmlgen = 0;
static char wmldir[PATH_MAX];

static void delete_old_cards(char *dirname)
{
	DIR             *bbcards;
	struct dirent   *d;
	struct stat     st;
	time_t		now = getcurrenttime(NULL);
	char		fn[PATH_MAX];

	bbcards = opendir(dirname);
	if (!bbcards) {
		errprintf("Cannot read directory %s\n", dirname);
		return;
        }

	chdir(dirname);
	while ((d = readdir(bbcards))) {
		strcpy(fn, d->d_name);
		stat(fn, &st);
		if ((fn[0] != '.') && S_ISREG(st.st_mode) && (st.st_mtime < (now-3600))) {
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

static void wml_header(FILE *output, char *cardid, int idpart)
{
	fprintf(output, "<?xml version=\"1.0\"?>\n");
	fprintf(output, "<!DOCTYPE wml PUBLIC \"-//WAPFORUM//DTD WML 1.1//EN\" \"http://www.wapforum.org/DTD/wml_1.1.xml\">\n");
	fprintf(output, "<wml>\n");
	fprintf(output, "<head>\n");
	fprintf(output, "<meta http-equiv=\"Cache-Control\" content=\"max-age=0\"/>\n");
	fprintf(output, "</head>\n");
	fprintf(output, "<card id=\"%s%d\" title=\"Hobbit\">\n", cardid, idpart);
}


static void generate_wml_statuscard(host_t *host, entry_t *entry)
{
	char fn[PATH_MAX];
	FILE *fd;
	char *msg = NULL, *logbuf = NULL;
	char l[MAX_LINE_LEN], lineout[MAX_LINE_LEN];
	char *p, *outp, *nextline;
	char hobbitdreq[1024];
	int hobbitdresult;

	sprintf(hobbitdreq, "hobbitdlog %s.%s", host->hostname, entry->column->name);
	hobbitdresult = sendmessage(hobbitdreq, NULL, NULL, &logbuf, 1, BBTALK_TIMEOUT);
	if ((hobbitdresult != BB_OK) || (logbuf == NULL) || (strlen(logbuf) == 0)) {
		errprintf("WML: Status not available\n");
		return;
	}

	msg = strchr(logbuf, '\n');
	if (msg) {
		msg++;
	}
	else {
		errprintf("WML: Unable to parse log data\n");
		xfree(logbuf);
		return;
	}

	nextline = msg;

	sprintf(fn, "%s/%s.%s.wml", wmldir, host->hostname, entry->column->name);
	fd = fopen(fn, "w");
	if (fd == NULL) {
		errprintf("Cannot create file %s\n", fn);
		return;
	}

	wml_header(fd, "name", 1);
	fprintf(fd, "<p align=\"center\">\n");
	fprintf(fd, "<anchor title=\"BB\">Host<go href=\"%s.wml\"/></anchor><br/>\n", host->hostname);
	fprintf(fd, "%s</p>\n", timestamp);
	fprintf(fd, "<p align=\"left\" mode=\"nowrap\">\n");
	fprintf(fd, "<b>%s.%s</b><br/></p><p mode=\"nowrap\">\n", host->hostname, entry->column->name);

	/*
	 * We need to parse the logfile a bit to get a decent WML
	 * card that contains the logfile. bbd does this for
	 * HTML, we need to do it ourselves for WML.
	 *
	 * Empty lines are removed.
	 * DOCTYPE lines (if any) are removed.
	 * "http://" is removed
	 * "<tr>" tags are replaced with a newline.
	 * All HTML tags are removed
	 * "&COLOR" is replaced with the shortname color
	 * "<", ">", "&", "\"" and "\'" are replaced with the coded name so they display correctly.
	 */
	while (nextline) {
		p = strchr(nextline, '\n'); if (p) *p = '\0';
		strcpy(l, nextline);
		if (p) nextline = p+1; else nextline = NULL;

		outp = lineout;

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
					char *endtag, *newstarttag;

					/*
					 * Possibilities:
					 * - <html tag>	: Drop it
					 * - <          : Output the &lt; equivalent
					 * - <<<        : Handle them one '<' at a time
					 */
					endtag = strchr(p+1, '>');
					newstarttag = strchr(p+1, '<');
					if ((endtag == NULL) || (newstarttag && (newstarttag < endtag))) {
						/* Single '<', or new starttag before the end */
						strcpy(outp, "&lt;");
						outp += 4; p++;
					}
					else {
						/* Drop all html tags */
						*outp = ' '; outp++;
						p = endtag+1;
					}
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

	fclose(fd);
	if (logbuf) xfree(logbuf);
}


void do_wml_cards(char *webdir)
{
	FILE		*bb2fd, *hostfd;
	char		bb2fn[PATH_MAX], hostfn[PATH_MAX];
	hostlist_t	*h;
	entry_t		*t;
	int		bb2wapcolor;
	long		wmlmaxchars = 1500;
	int		bb2part = 1;

	/* Determine where the WML files go */
	sprintf(wmldir, "%s/wml", webdir);

	/* Make sure the WML directory exists */
	if (chdir(wmldir) != 0) mkdir(wmldir, 0755);
	if (chdir(wmldir) != 0) {
		errprintf("Cannot access or create the WML output directory %s\n", wmldir);
		return;
	}

	/* Make sure this is set sensibly */
	if (xgetenv("WMLMAXCHARS")) {
		wmlmaxchars = atol(xgetenv("WMLMAXCHARS"));
	}

	/*
	 * Cleanup cards that are too old.
	 */
	delete_old_cards(wmldir);

	/* 
	 * Find all the test entries that belong on the WAP page,
	 * and calculate the color for the bb2 wap page.
	 *
	 * We want only tests that have the "onwap" flag set, i.e.
	 * tests given in the "WAP:test,..." for this host (of the
	 * "NK:test,..." if no WAP list).
	 *
	 * At the same time, generate the WML card for the tests,
	 * corresponding to the HTML file for the test logfile.
	 */
	bb2wapcolor = COL_GREEN;
	for (h = hostlistBegin(); (h); h = hostlistNext()) {
		h->hostentry->wapcolor = COL_GREEN;
		for (t = h->hostentry->entries; (t); t = t->next) {
			if (t->onwap && ((t->color == COL_RED) || (t->color == COL_YELLOW))) {
				generate_wml_statuscard(h->hostentry, t);
				h->hostentry->anywaps = 1;
			}
			else {
				/* Clear the onwap flag - makes testing later a bit simpler */
				t->onwap = 0;
			}

			if (t->onwap && (t->color > h->hostentry->wapcolor)) h->hostentry->wapcolor = t->color;
		}

		/* Update the bb2wapcolor */
		if ( (h->hostentry->wapcolor == COL_RED) || (h->hostentry->wapcolor == COL_YELLOW) ) {
			if (h->hostentry->wapcolor > bb2wapcolor) bb2wapcolor = h->hostentry->wapcolor;
		}
	}

	/* Start the BB2 WML card */
	sprintf(bb2fn, "%s/bb2.wml.tmp", wmldir);
	bb2fd = fopen(bb2fn, "w");
	if (bb2fd == NULL) {
		errprintf("Cannot open BB2 WML file %s\n", bb2fn);
		return;
	}

	/* Standard BB2 wap header */
	wml_header(bb2fd, "card", bb2part);
	fprintf(bb2fd, "<p align=\"center\" mode=\"nowrap\">\n");
	fprintf(bb2fd, "%s</p>\n", timestamp);
	fprintf(bb2fd, "<p align=\"center\" mode=\"nowrap\">\n");
	fprintf(bb2fd, "Summary Status<br/><b>%s</b><br/><br/>\n", colorname(bb2wapcolor));

	/* All green ? Just say so */
	if (bb2wapcolor == COL_GREEN) {
		fprintf(bb2fd, "All is OK<br/>\n");
	}

	/* Now loop through the hostlist again, and generate the bb2wap links and host pages */
	for (h = hostlistBegin(); (h); h = hostlistNext()) {
		if (h->hostentry->anywaps) {

			/* Create the host WAP card, with links to individual test results */
			sprintf(hostfn, "%s/%s.wml", wmldir, h->hostentry->hostname);
			hostfd = fopen(hostfn, "w");
			if (hostfd == NULL) {
				errprintf("Cannot create file %s\n", hostfn);
				return;
			}

			wml_header(hostfd, "name", 1);
			fprintf(hostfd, "<p align=\"center\">\n");
			fprintf(hostfd, "<anchor title=\"BB\">Overall<go href=\"bb2.wml\"/></anchor><br/>\n");
			fprintf(hostfd, "%s</p>\n", timestamp);
			fprintf(hostfd, "<p align=\"left\" mode=\"nowrap\">\n");
			fprintf(hostfd, "<b>%s</b><br/><br/>\n", h->hostentry->hostname);

			for (t = h->hostentry->entries; (t); t = t->next) {
				if (t->onwap) {
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

			/* Create the link from the bb2 wap card to the host card */
			fprintf(bb2fd, "<b><anchor title=\"%s\">%s<go href=\"%s.wml\"/></anchor></b> %s<br/>\n", 
				h->hostentry->hostname, wml_colorname(h->hostentry->wapcolor), 
				h->hostentry->hostname, h->hostentry->hostname);

			/* 
			 * Gross hack. Some WAP phones cannot handle large cards. 
			 * So if the card grows larger than WMLMAXCHARS, split it into 
			 * multiple files and link from one file to the next.
			 */
			if (ftello(bb2fd) >= wmlmaxchars) {
				char oldbb2fn[PATH_MAX];

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
					return;
				}
				wml_header(bb2fd, "card", bb2part);
				fprintf(bb2fd, "<p align=\"center\">\n");
				fprintf(bb2fd, "<anchor title=\"Prev\">Previous<go href=\"%s\"/></anchor><br/>\n", oldbb2fn);
				fprintf(bb2fd, "%s</p>\n", timestamp);
				fprintf(bb2fd, "<p align=\"center\" mode=\"nowrap\">\n");
				fprintf(bb2fd, "Summary Status<br/><b>%s</b><br/><br/>\n", colorname(bb2wapcolor));
			}
		}
	}

	fprintf(bb2fd, "</p> </card> </wml>\n");
	fclose(bb2fd);

	if (chdir(wmldir) == 0) {
		/* Rename the top-level file into place now */
		rename("bb2.wml.tmp", "bb2.wml");

		/* Make sure there is the index.wml file pointing to bb2.wml */
		symlink("bb2.wml", "index.wml");
	}

	return;
}

