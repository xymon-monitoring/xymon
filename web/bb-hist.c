/*----------------------------------------------------------------------------*/
/* Big Brother webpage generator tool.                                        */
/*                                                                            */
/* This is a replacement for the "bb-hist.sh" script                          */
/*                                                                            */
/* Primary reason for doing this: Shell scripts perform badly, and with a     */
/* medium-sized installation (~150 hosts) it takes several minutes to         */
/* generate the webpages. This is a problem, when the pages are used for      */
/* 24x7 monitoring of the system status.                                      */
/*                                                                            */
/* Copyright (C) 2003 Henrik Storner <henrik@storner.dk>                      */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: bb-hist.c,v 1.19 2003-08-05 09:52:15 henrik Exp $";

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "bbgen.h"
#include "util.h"
#include "reportdata.h"
#include "debug.h"

static char selfurl[MAX_PATH];
static int startoffset = 0;
static int len1d = 24;
static char *bartitle1d = "1 day";
static char *summarytitle1d = "1 day summary";
static int len1w = 7;
static char *bartitle1w = "1 week";
static char *summarytitle1w = "1 week summary";
static int len4w = 28;
static char *bartitle4w = "4 weeks";
static char *summarytitle4w = "4 week summary";
static int len1y = 12;
static char *bartitle1y = "1 year";
static char *summarytitle1y = "1 year summary";

#ifndef DEFPIXELS
static int usepct = 1;
static int pixels = 100;
#else
static int usepct = 0;
static int pixels = DEFPIXELS;
#endif

/* What colorbars and summaries to show by default */
#define BARSUM_1D 0x0001	/* 1-day bar */
#define BARSUM_1W 0x0002	/* 1-week bar */
#define BARSUM_4W 0x0004	/* 4-week bar */
#define BARSUM_1Y 0x0008	/* 1-year bar */

#ifndef DEFBARSUMS
#define DEFBARSUMS (BARSUM_1D|BARSUM_1W)
#endif

static unsigned int barsums = DEFBARSUMS;


static void generate_colorbar(
			FILE *htmlrep,		/* Output file */
			time_t periodlen, 	/* Length of one peiod on the bar. Usually 1 hour/day/month */
			int periodcount,	/* # of periodlen items on the bar */
			time_t startofperiod, 	/* Starttime of the last (incomplete) period */
			time_t startofbar, 	/* Start of the colorbar */
			time_t today,		/* End of the colorbar */
			replog_t *periodlog,	/* Log entries for period */
			char *caption,		/* Title */
			char *tagfmt)		/* strftime() formatstring for tags */
{
	/* Generate the colorbar "graph" */

	int secsperpixel, periodpixels, pixelsfirst, pixelslast, pixelssum;
	replog_t *colorlog, *walk, *tmp;
	char *pctstr = "";

	/*
	 * Pixel-based charts are better, but for backwards
	 * compatibility allow for a graph that has 100 "pixels"
	 * and adds a "%" to the width specs.
	 */
	if (usepct) {
		pixels = 100;
		pctstr = "%";
	}

	secsperpixel = (periodlen*periodcount / pixels);	/* How many seconds required for 1 pixel */
	periodpixels = (pixels / periodcount);			/* Same as: periodlen / secsperpixel */

	/* Compute the percentage of the first and last (incomplete) periods */
	pixelslast = (today - startofperiod) / secsperpixel;
	pixelsfirst = periodpixels - pixelslast;

	/* Need to re-sort the period-log to chronological order */
	colorlog = NULL;
	for (walk = periodlog; (walk); walk = tmp) {
		tmp = walk->next;
		walk->next = colorlog;
		colorlog = walk;
		walk = tmp;
	}

	fprintf(htmlrep, "<TABLE WIDTH=\"%d%s\" BORDER=0 BGCOLOR=\"#666666\">\n", pixels, pctstr);
	fprintf(htmlrep, "<TR><TD ALIGN=CENTER>\n");

	/* The date stamps */
	fprintf(htmlrep, "<TABLE WIDTH=\"100%%\" BORDER=0 FRAME=NONE CELLSPACING=0 CELLPADDING=1 BGCOLOR=\"#000033\">\n");
	fprintf(htmlrep, "<TR>\n");

	fprintf(htmlrep, "<TD WIDTH=\"34%%\" ALIGN=LEFT>");
	if (colorlog && colorlog->starttime <= startofbar) {
		fprintf(htmlrep, "<A HREF=\"%s&amp;OFFSET=%ld\">", selfurl, startoffset+(periodlen*periodcount/86400));
	}
	fprintf(htmlrep, "<B>%s</B>", ctime(&startofbar));
	if (colorlog && colorlog->starttime <= startofbar) fprintf(htmlrep, "</A>");
	fprintf(htmlrep, "</TD>\n");

	fprintf(htmlrep, "<TH ALIGN=CENTER WIDTH=\"32%%\">%s</TH>\n", caption);

	fprintf(htmlrep, "<TD WIDTH=\"34%%\" ALIGN=RIGHT>\n");
	if (startoffset > 0) {
		fprintf(htmlrep, "<A HREF=\"%s&amp;OFFSET=%ld\">", selfurl, startoffset-(periodlen*periodcount/86400));
	}
	fprintf(htmlrep, "<B>%s</B>\n", ctime(&today));
	if (startoffset > 0) fprintf(htmlrep, "</A>");
	fprintf(htmlrep, "</TD>\n");

	fprintf(htmlrep, "</TR>\n");
	fprintf(htmlrep, "</TABLE>\n");


	/* The period marker line */
	pixelssum = pixelsfirst + pixelslast;
	fprintf(htmlrep, "<TABLE WIDTH=\"100%%\" BORDER=0 FRAME=NONE CELLSPACING=0 CELLPADDING=0 BGCOLOR=\"#000033\">\n");
	fprintf(htmlrep, "<TR>\n");
	fprintf(htmlrep, "<TD WIDTH=\"%d%s\" ALIGN=CENTER BGCOLOR=\"#000000\"><B>&nbsp;</B></TD>\n", pixelsfirst, pctstr);
	{
		int i; 
		time_t markertime;
		char tag[20];
		char *bgcols[2] = { "\"#000000\"", "\"#555555\"" };
		int curbg = 1;

		markertime = startofbar;
		for (i=1; i<periodcount; i++) {
			markertime += periodlen;
			strftime(tag, sizeof(tag), tagfmt, localtime(&markertime));
			fprintf(htmlrep, "<TD WIDTH=\"%d%s\" ALIGN=CENTER BGCOLOR=%s><B>%s</B></TD>\n", 
				periodpixels, pctstr, bgcols[curbg], tag);
			pixelssum += periodpixels;
			curbg = (1-curbg);
		}

		markertime += periodlen;
		strftime(tag, sizeof(tag), tagfmt, localtime(&markertime));
		fprintf(htmlrep, "<TD WIDTH=\"%d%s\" ALIGN=CENTER BGCOLOR=%s><B>%s</B></TD>\n", 
			pixelslast, pctstr, bgcols[curbg], tag);
	}
	fprintf(htmlrep, "<!-- pixelssum = %d -->\n", pixelssum);
	fprintf(htmlrep, "</TR>\n");
	fprintf(htmlrep, "</TABLE>\n");


	/* The actual color bar */
	fprintf(htmlrep, "<TABLE WIDTH=\"100%%\" BORDER=0 FRAME=NONE CELLSPACING=0 CELLPADDING=0 BGCOLOR=\"#000033\">\n");
	fprintf(htmlrep, "<TR>\n");
	pixelssum = 0;

	/* First entry may not start at our report-start time */
	if (colorlog == NULL) {
		/* No data for period - all white */
		pixelssum += secsperpixel;
		fprintf(htmlrep, "<TD WIDTH=100%% BGCOLOR=white NOWRAP>&nbsp</TD>\n");
	}
	else if (colorlog->starttime > startofbar) {
		/* Data starts after the bar does - so a white period in front */
		int pixels = ((colorlog->starttime - startofbar) / secsperpixel);

		if (((colorlog->starttime - startofbar) >= (secsperpixel/2)) && (pixels == 0)) pixels = 1;
		if (pixels > 0) {
			pixelssum += pixels;
			fprintf(htmlrep, "<TD WIDTH=\"%d%s\" BGCOLOR=%s NOWRAP>&nbsp</TD>\n", pixels, pctstr, "white");
		}
	}

	for (walk = colorlog; (walk); walk = walk->next) {
		/* Show each interval we have data for */

		int pixels = (walk->duration / secsperpixel);

		/* Intervals that give between 0.5 and 1 pixel are enlarged */
		if ((walk->duration >= (secsperpixel/2)) && (pixels == 0)) pixels = 1;

		if (pixels > 0) {
			pixelssum += pixels;
			fprintf(htmlrep, "<TD WIDTH=\"%d%s\" BGCOLOR=%s NOWRAP>&nbsp</TD>\n", 
				pixels, pctstr, ((walk->color == COL_CLEAR) ? "white" : colorname(walk->color)));
		}
	}

	fprintf(htmlrep, "<!-- pixelssum = %d -->\n", pixelssum);
	fprintf(htmlrep, "</TR>\n");
	fprintf(htmlrep, "</TABLE>\n");

	fprintf(htmlrep, "</TD></TR></TABLE>\n");
	fprintf(htmlrep, "<BR><BR>\n");

}

static void generate_pct_summary(
			FILE *htmlrep,			/* output file */
			char *hostname,
			char *service,
			char *caption,
			reportinfo_t *repinfo, 		/* Percent summaries for period */
			int first, int last)
{
	if (first) {
		fprintf(htmlrep, "<TABLE BORDER=0 BGCOLOR=\"#000033\" CELLSPACING=5>\n");
		fprintf(htmlrep, "<TR><TD>\n");
		fprintf(htmlrep, "<TABLE BORDER=0 BGCOLOR=\"#000000\" CELLPADDING=3>\n");
	}

	fprintf(htmlrep, "<TR BGCOLOR=\"#333333\"><TD COLSPAN=6 ALIGN=CENTER><B>%s</B></TD></TR>\n", caption);
	fprintf(htmlrep, "<TR BGCOLOR=\"#000000\">\n");
	fprintf(htmlrep, "<TD ALIGN=CENTER><IMG SRC=\"%s/%s\" ALT=\"%s\" HEIGHT=%s WIDTH=%s BORDER=0></TD>\n", 
		getenv("BBSKIN"), dotgiffilename(COL_GREEN, 0, 1), colorname(COL_GREEN), getenv("DOTHEIGHT"), getenv("DOTWIDTH"));
	fprintf(htmlrep, "<TD ALIGN=CENTER><IMG SRC=\"%s/%s\" ALT=\"%s\" HEIGHT=%s WIDTH=%s BORDER=0></TD>\n", 
		getenv("BBSKIN"), dotgiffilename(COL_YELLOW, 0, 1), colorname(COL_YELLOW), getenv("DOTHEIGHT"), getenv("DOTWIDTH"));
	fprintf(htmlrep, "<TD ALIGN=CENTER><IMG SRC=\"%s/%s\" ALT=\"%s\" HEIGHT=%s WIDTH=%s BORDER=0></TD>\n", 
		getenv("BBSKIN"), dotgiffilename(COL_RED, 0, 1), colorname(COL_RED), getenv("DOTHEIGHT"), getenv("DOTWIDTH"));
	fprintf(htmlrep, "<TD ALIGN=CENTER><IMG SRC=\"%s/%s\" ALT=\"%s\" HEIGHT=%s WIDTH=%s BORDER=0></TD>\n", 
		getenv("BBSKIN"), dotgiffilename(COL_PURPLE, 0, 1), colorname(COL_PURPLE), getenv("DOTHEIGHT"), getenv("DOTWIDTH"));
	fprintf(htmlrep, "<TD ALIGN=CENTER><IMG SRC=\"%s/%s\" ALT=\"%s\" HEIGHT=%s WIDTH=%s BORDER=0></TD>\n", 
		getenv("BBSKIN"), dotgiffilename(COL_CLEAR, 0, 1), colorname(COL_CLEAR), getenv("DOTHEIGHT"), getenv("DOTWIDTH"));
	fprintf(htmlrep, "<TD ALIGN=CENTER><IMG SRC=\"%s/%s\" ALT=\"%s\" HEIGHT=%s WIDTH=%s BORDER=0></TD>\n", 
		getenv("BBSKIN"), dotgiffilename(COL_BLUE, 0, 1), colorname(COL_BLUE), getenv("DOTHEIGHT"), getenv("DOTWIDTH"));
	fprintf(htmlrep, "</TR>\n");
	fprintf(htmlrep, "<TR BGCOLOR=\"#000033\">\n");
	fprintf(htmlrep, "<TD ALIGN=CENTER><B>%.2f%%</B></TD>\n", repinfo->fullpct[COL_GREEN]);
	fprintf(htmlrep, "<TD ALIGN=CENTER><B>%.2f%%</B></TD>\n", repinfo->fullpct[COL_YELLOW]);
	fprintf(htmlrep, "<TD ALIGN=CENTER><B>%.2f%%</B></TD>\n", repinfo->fullpct[COL_RED]);
	fprintf(htmlrep, "<TD ALIGN=CENTER><B>%.2f%%</B></TD>\n", repinfo->fullpct[COL_PURPLE]);
	fprintf(htmlrep, "<TD ALIGN=CENTER><B>%.2f%%</B></TD>\n", repinfo->fullpct[COL_CLEAR]);
	fprintf(htmlrep, "<TD ALIGN=CENTER><B>%.2f%%</B></TD>\n", repinfo->fullpct[COL_BLUE]);
	fprintf(htmlrep, "</TR>\n");

	if (last) {
		fprintf(htmlrep, "</TD></TR>\n");
		fprintf(htmlrep, "</TABLE>\n");

		fprintf(htmlrep, "<TR BGCOLOR=\"#000000\">\n");
		fprintf(htmlrep, "<TD ALIGN=CENTER>\n");
		fprintf(htmlrep, "<FONT %s><B>[Totals may not equal 100%%]</B></FONT></TD> </TR>\n", getenv("MKBBCOLFONT"));
		fprintf(htmlrep, "</TABLE>\n");
	}
	else {
		fprintf(htmlrep, "<TR BGCOLOR=\"#000000\" BORDER=0><TD COLSPAN=6>&nbsp;</TD></TR>\n");
	}
}


static void generate_histlog_table(FILE *htmlrep,
		char *hostname, char *service, int entrycount, replog_t *loghead)
{
	char *bgcols[2] = { "\"#000000\"", "\"#000033\"" };
	int curbg = 0;
	replog_t *walk;

	fprintf(htmlrep, "<TABLE BORDER=0 BGCOLOR=\"#333333\" CELLSPACING=3>\n");
	fprintf(htmlrep, "<TR>\n");
	if (entrycount) {
		fprintf(htmlrep, "<TD COLSPAN=3 ALIGN=CENTER><B>Last %d log entries</B> ", entrycount);
		fprintf(htmlrep, "<A HREF=\"%s/bb-hist.sh?HISTFILE=%s.%s&amp;ENTRIES=all\">(Full HTML log)</A></TD>\n", 
			getenv("CGIBINURL"), commafy(hostname), service);
	}
	else {
		fprintf(htmlrep, "<TD COLSPAN=3 ALIGN=CENTER><B>All log entries</B></TD>\n");
	}
	fprintf(htmlrep, "</TR>\n");
	fprintf(htmlrep, "<TR BGCOLOR=\"#333333\">\n");
	fprintf(htmlrep, "<TD ALIGN=CENTER><FONT %s><B>Date</B></FONT></TD>\n", getenv("MKBBCOLFONT"));
	fprintf(htmlrep, "<TD ALIGN=CENTER><FONT %s><B>Status</B></FONT></TD>\n", getenv("MKBBCOLFONT"));
	fprintf(htmlrep, "<TD ALIGN=CENTER><FONT %s><B>Duration</B></FONT></TD>\n", getenv("MKBBCOLFONT"));
	fprintf(htmlrep, "</TR>\n");

	for (walk = loghead; (walk); walk = walk->next) {
		char start[30];

		strftime(start, sizeof(start), "%a %b %d %H:%M:%S %Y", localtime(&walk->starttime));

		fprintf(htmlrep, "<TR BGCOLOR=%s>\n", bgcols[curbg]); curbg = (1-curbg);
		fprintf(htmlrep, "<TD ALIGN=LEFT NOWRAP>%s</TD>\n", start);
		fprintf(htmlrep, "<TD ALIGN=CENTER BGCOLOR=\"#000000\">");
		fprintf(htmlrep, "<A HREF=\"%s/bb-histlog.sh?HOST=%s&amp;SERVICE=%s&amp;TIMEBUF=%s\">", 
			getenv("CGIBINURL"), hostname, service, walk->timespec);
		fprintf(htmlrep, "<IMG SRC=\"%s/%s\" ALT=\"%s\" HEIGHT=%s WIDTH=%s BORDER=0>", 
			getenv("BBSKIN"), dotgiffilename(walk->color, 0, 1), colorname(walk->color),
			getenv("DOTHEIGHT"), getenv("DOTWIDTH"));
		fprintf(htmlrep, "</A></TD>\n");

		fprintf(htmlrep, "<TD ALIGN=CENTER>%s</TD>\n", durationstr(walk->duration));
		fprintf(htmlrep, "</TR>\n\n");
	}


	fprintf(htmlrep, "</TABLE>\n");
}


void generate_history(FILE *htmlrep, 			/* output file */
		      char *hostname, char *service, 	/* Host and service we report on */
		      char *ip, 			/* IP - for the header only */
		      time_t today,			/* End time of color-bar graphs */

                      time_t start1d,			/* Starttime of 1-day period */
		      reportinfo_t *repinfo1d, 		/* Percent summaries for 1-day period */
		      replog_t *log1d, 			/* Events during past 1 day */

                      time_t start1w,			/* Starttime of 1-week period */
		      reportinfo_t *repinfo1w, 		/* Percent summaries for 1-week period */
		      replog_t *log1w, 			/* Events during past 1 week */

                      time_t start4w,			/* Starttime of 4-week period */
		      reportinfo_t *repinfo4w, 		/* Percent summaries for 4-week period */
		      replog_t *log4w, 			/* Events during past 4 weeks */

                      time_t start1y,			/* Starttime of 1-year period */
		      reportinfo_t *repinfo1y, 		/* Percent summaries for 1-year period */
		      replog_t *log1y, 			/* Events during past 1 yeary */

		      int entrycount,			/* Log entry maxcount */
		      replog_t *loghead)		/* Eventlog for entrycount events back */
{
	time_t startofperiod;
	struct tm *tmbuf;

	sethostenv(hostname, ip, service, colorname(COL_GREEN));
	headfoot(htmlrep, "hist", "", "header", COL_GREEN);

	fprintf(htmlrep, "\n");
	fprintf(htmlrep, "<CENTER>\n");
	fprintf(htmlrep, "<BR><FONT %s><B>%s - %s</B></FONT>\n", getenv("MKBBROWFONT"), hostname, service);
	fprintf(htmlrep, "<BR><BR>\n");

	/* Create the color-bars */

	if (log1d) {
		/* 1-day bar: Last period starts at beginning of hour, periods are 1 hour long */
		tmbuf = localtime(&today); 
		tmbuf->tm_min = tmbuf->tm_sec = 0; 
		startofperiod = mktime(tmbuf);
		generate_colorbar(htmlrep, 3600, len1d, startofperiod, start1d, today, log1d, bartitle1d, "%H");
	}

	if (log1w) {
		/* 1-week bar: Last period starts at beginning of day, periods are 1 day long */
		tmbuf = localtime(&today); 
		tmbuf->tm_hour = tmbuf->tm_min = tmbuf->tm_sec = 0;
		startofperiod = mktime(tmbuf);
		generate_colorbar(htmlrep, 86400, len1w, startofperiod, start1w, today, log1w, bartitle1w, "%a");
	}

	if (log4w) {
		/* 4-week bar: Last period starts at beginning of day, periods are 1 day long */
		tmbuf = localtime(&today); 
		tmbuf->tm_hour = tmbuf->tm_min = tmbuf->tm_sec = 0;
		startofperiod = mktime(tmbuf);
		generate_colorbar(htmlrep, 86400, len4w, startofperiod, start4w, today, log4w, bartitle4w, "%d");
	}

	if (log1y) {
		/* 1-year bar: Last period starts at beginning of month, periods are 30 days long */
		tmbuf = localtime(&today); 
		tmbuf->tm_hour = tmbuf->tm_min = tmbuf->tm_sec = 0;
		tmbuf->tm_mday = 1;
		startofperiod = mktime(tmbuf);
		generate_colorbar(htmlrep, 30*86400, len1y, startofperiod, start1y, today, log1y, bartitle1y, "%b");
	}


	/* Availability percentage summary */
	fprintf(htmlrep, "<CENTER>\n");
	{
		int isfirst;
		int islast[4];

		isfirst = 1;
		islast[0] = islast[1] = islast[2] = islast[3] = 0;

		/*
		 * We (ab)use the log* params to see if we should generate a summary
		 * for the period - even though the summary is not using the log* data!
		 */
		if (log1y) islast[3] = 1;
		else if (log4w) islast[2] = 1;
		else if (log1w) islast[1] = 1;
		else islast[0] = 1;

		if (log1d) {
			generate_pct_summary(htmlrep, hostname, service, summarytitle1d, repinfo1d, isfirst, islast[0]);
			isfirst = 0;
		}
		if (log1w) {
			generate_pct_summary(htmlrep, hostname, service, summarytitle1w, repinfo1w, isfirst, islast[1]);
			isfirst = 0;
		}
		if (log4w) {
			generate_pct_summary(htmlrep, hostname, service, summarytitle4w, repinfo4w, isfirst, islast[2]);
			isfirst = 0;
		}
		if (log1y) {
			generate_pct_summary(htmlrep, hostname, service, summarytitle1y, repinfo1y, isfirst, islast[3]);
			isfirst = 0;
		}
	}
	fprintf(htmlrep, "</CENTER>\n");

	fprintf(htmlrep, "<BR><BR>\n");


	/* Last N histlog entries */
	fprintf(htmlrep, "<CENTER>\n");
	generate_histlog_table(htmlrep, hostname, service, entrycount, loghead);
	fprintf(htmlrep, "</CENTER>\n");

	fprintf(htmlrep, "<BR><BR>\n");

	/* BBHISTEXT extensions */
	do_bbext(htmlrep, "BBHISTEXT", "hist");

	fprintf(htmlrep, "</CENTER>\n");

	headfoot(htmlrep, "histlog", "", "footer", COL_GREEN);
}


/*
 * This program is invoked via CGI with QUERY_STRING containing:
 *
 *	HISTFILE=www,sample,com.conn
 *	ENTRIES=50
 */

/* These are dummy vars needed by stuff in util.c */
hostlist_t      *hosthead = NULL;
link_t          *linkhead = NULL;
link_t  null_link = { "", "", "", NULL };

double reportgreenlevel = 99.995;
double reportwarnlevel = 98.0;

char *hostname = "";
char *service = "";
char *ip = "";
int entrycount = 50;

char *reqenv[] = {
"BBHIST",
"BBHISTLOGS",
"BBREP",
"BBREPURL",
"BBSKIN",
"CGIBINURL",
"DOTWIDTH",
"DOTHEIGHT",
"MKBBCOLFONT",
"MKBBROWFONT",
NULL };

static void errormsg(char *msg)
{
	printf("Content-type: text/html\n\n");
	printf("<html><head><title>Invalid request</title></head>\n");
	printf("<body>%s</body></html>\n", msg);
	exit(1);
}

static void parse_query(void)
{
	char *query, *token;

	if (getenv("QUERY_STRING") == NULL) {
		errormsg("Invalid request");
		return;
	}
	else query = urldecode("QUERY_STRING");

	if (!urlvalidate(query, NULL)) {
		errormsg("Invalid request");
		return;
	}

	token = strtok(query, "&");
	while (token) {
		char *val;
		
		val = strchr(token, '='); if (val) { *val = '\0'; val++; }

		if (argnmatch(token, "HISTFILE")) {
			char *p = strrchr(val, '.');

			if (p) { *p = '\0'; service = malcop(p+1); }
			hostname = malcop(val);
			while ((p = strchr(hostname, ','))) *p = '.';
		}
		else if (argnmatch(token, "IP")) {
			ip = malcop(val);
		}
		else if (argnmatch(token, "ENTRIES")) {
			if (strcmp(val, "all") == 0) entrycount = 0;
			else entrycount = atoi(val);
		}
		else if (argnmatch(token, "PIXELS")) {
			pixels = atoi(val);
			if (pixels > 0) usepct = 0; else usepct = 1;
		}
		else if (argnmatch(token, "OFFSET")) {
			startoffset = atoi(val);
		}
		else if (argnmatch(token, "BARSUMS")) {
			barsums = atoi(val);
		}

		token = strtok(NULL, "&");
	}

	free(query);
}


int main(int argc, char *argv[])
{
	char histlogfn[MAX_PATH];
	char tailcmd[MAX_PATH];
	FILE *fd;
	reportinfo_t repinfo1d, repinfo1w, repinfo4w, repinfo1y, dummyrep;
	time_t now;
	time_t start1d, start1w, start4w, start1y;
	replog_t *log1d, *log1w, *log4w, *log1y;
	struct tm *starttm;
	char *p;

	envcheck(reqenv);
	parse_query();


	/* Build our own URL */
	sprintf(selfurl, "%s/bb-hist.sh?HISTFILE=%s.%s", getenv("CGIBINURL"), commafy(hostname), service);

	p = selfurl + strlen(selfurl);
	sprintf(p, "&amp;BARSUMS=%d", barsums);

	if (strlen(ip)) {
		p = selfurl + strlen(selfurl);
		sprintf(p, "&amp;IP=%s", ip);
	}

	if (entrycount) {
		p = selfurl + strlen(selfurl);
		sprintf(p, "&amp;ENTRIES=%d", entrycount);
	}
	else strcat(selfurl, "&amp;ENTRIES=ALL");

	if (usepct) {
		/* Must modify 4-week charts to be 5-weeks, or the last day is 19% of the bar */
		/*
		 * Percent-based charts look awful with 24 hours / 7 days / 28 days / 12 months as basis
		 * because these numbers dont divide into 100 neatly. So the last item becomes
		 * too large (worst with the 28-day char: 100/28 = 3, last becomes (100-27*3) = 19% wide).
		 * So adjust the periods to something that matches percent-based calculations better.
		 */
		len1d = 25; bartitle1d = "25 hours"; summarytitle1d = "25 hour summary";
		len1w = 10; bartitle1w = "10 days"; summarytitle1w = "10 day summary";
		len4w = 33; bartitle4w = "33 days"; summarytitle4w = "33 day summary";
		len1y = 10; bartitle1y = "10 months"; summarytitle1y = "10 month summary";
		strcat(selfurl, "&amp;PIXELS=0");
	}
	else {
		p = selfurl + strlen(selfurl);
		sprintf(p, "&amp;PIXELS=%d", pixels);
	}

	sprintf(histlogfn, "%s/%s.%s", getenv("BBHIST"), commafy(hostname), service);
	fd = fopen(histlogfn, "r");
	if (fd == NULL) {
		errormsg("Cannot open history file");
	}

	log1d = log1w = log4w = log1y = NULL;
	now = time(NULL) - startoffset*86400;
	starttm = localtime(&now); starttm->tm_hour -= len1d; start1d = mktime(starttm);
	starttm = localtime(&now); starttm->tm_mday -= len1w; start1w = mktime(starttm);
	starttm = localtime(&now); starttm->tm_mday -= len4w; start4w = mktime(starttm);
	starttm = localtime(&now); starttm->tm_mday -= 30*len1y; start1y = mktime(starttm);

	/*
	 * Collect data for the color-bars and summaries. Multiple scans over the history file,
	 * but doing it all in one go would be hideously complex.
	 */
	if (barsums & BARSUM_1D) {
		parse_historyfile(fd, &repinfo1d, NULL, NULL, start1d, now, 1, reportwarnlevel, reportgreenlevel, NULL);
		log1d = save_replogs();
	}

	if (barsums & BARSUM_1W) {
		parse_historyfile(fd, &repinfo1w, NULL, NULL, start1w, now, 1, reportwarnlevel, reportgreenlevel, NULL);
		log1w = save_replogs();
	}

	if (barsums & BARSUM_4W) {
		parse_historyfile(fd, &repinfo4w, NULL, NULL, start4w, now, 1, reportwarnlevel, reportgreenlevel, NULL);
		log4w = save_replogs();
	}

	if (barsums & BARSUM_1Y) {
		parse_historyfile(fd, &repinfo1y, NULL, NULL, start1y, now, 1, reportwarnlevel, reportgreenlevel, NULL);
		log1y = save_replogs();
	}

	if (entrycount == 0) {
		/* All entries - just rewind the history file and do all of them */
		rewind(fd);
		parse_historyfile(fd, &dummyrep, NULL, NULL, 0, time(NULL), 1, reportwarnlevel, reportgreenlevel, NULL);
		fclose(fd);
	}
	else {
		/* Last 50 entries - we cheat and use "tail" in a pipe to pick the entries */
		fclose(fd);
		sprintf(tailcmd, "tail -%d %s", entrycount, histlogfn);
		fd = popen(tailcmd, "r");
		if (fd == NULL) errormsg("Cannot run tail on the histfile");
		parse_historyfile(fd, &dummyrep, NULL, NULL, 0, time(NULL), 1, reportwarnlevel, reportgreenlevel, NULL);
		pclose(fd);
	}


	/* Now generate the webpage */
	printf("Content-Type: text/html\n\n");

	generate_history(stdout, 
			 hostname, service, ip, now, 
			 start1d, &repinfo1d, log1d, 
			 start1w, &repinfo1w, log1w, 
			 start4w, &repinfo4w, log4w, 
			 start1y, &repinfo1y, log1y, 
			 entrycount, reploghead);

	return 0;
}

