/*----------------------------------------------------------------------------*/
/* Big Brother webpage generator tool.                                        */
/*                                                                            */
/* This is a replacement for the "bb-replog.sh" script                        */
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

static char rcsid[] = "$Id: bb-replog.c,v 1.7 2003-06-23 15:00:18 henrik Exp $";

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "bbgen.h"
#include "util.h"
#include "reportdata.h"
#include "debug.h"
#include "bb-replog.h"

char *stylenames[3] = { "crit", "nongr", "all" };


void generate_replog(FILE *htmlrep, FILE *textrep, char *textrepurl,
		     char *hostname, char *ip, char *service, int color, int style,
		     time_t st, time_t end, double reportwarnlevel, double reportgreenlevel, 
		     reportinfo_t *repinfo)
{
	replog_t *walk;
	char *bgcols[2] = { "\"#000000\"", "\"#000033\"" };
	int curbg = 0;

	sethostenv(hostname, ip, service, colorname(color));
	sethostenv_report(st, end, reportwarnlevel, reportgreenlevel);

	headfoot(htmlrep, "replog", "", "header", color);

	fprintf(htmlrep, "\n");

	fprintf(htmlrep, "<CENTER>\n");
	fprintf(htmlrep, "<BR><FONT %s><H2>%s - %s</H2></FONT>\n", getenv("MKBBROWFONT"), hostname, service);
	fprintf(htmlrep, "<TABLE BORDER=0 BGCOLOR=\"#333333\" CELLPADDING=3>\n");
	fprintf(htmlrep, "<TR BGCOLOR=\"#000000\">\n");
	fprintf(htmlrep, "</TR>\n");
	fprintf(htmlrep, "<TR></TR>\n");
	fprintf(htmlrep, "<TR>\n");
	fprintf(htmlrep, "<TD COLSPAN=6><CENTER><B>Overall Availability: %.2f%%</CENTER></TD></TR>\n", repinfo->availability);
	fprintf(htmlrep, "<TR BGCOLOR=\"#000033\">\n");
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
	fprintf(htmlrep, "<TD ALIGN=CENTER><B>%.2f%%</B></TD>\n", repinfo->pct[COL_GREEN]);
	fprintf(htmlrep, "<TD ALIGN=CENTER><B>%.2f%%</B></TD>\n", repinfo->pct[COL_YELLOW]);
	fprintf(htmlrep, "<TD ALIGN=CENTER><B>%.2f%%</B></TD>\n", repinfo->pct[COL_RED]);
	fprintf(htmlrep, "<TD ALIGN=CENTER><B>%.2f%%</B></TD>\n", repinfo->pct[COL_PURPLE]);
	fprintf(htmlrep, "<TD ALIGN=CENTER><B>%.2f%%</B></TD>\n", repinfo->pct[COL_CLEAR]);
	fprintf(htmlrep, "<TD ALIGN=CENTER><B>%.2f%%</B></TD>\n", repinfo->pct[COL_BLUE]);
	fprintf(htmlrep, "</TR>\n");
	fprintf(htmlrep, "<TR BGCOLOR=\"#000000\">\n");
	fprintf(htmlrep, "<TD ALIGN=CENTER><B>Event count</B></TD>\n");
	fprintf(htmlrep, "<TD ALIGN=CENTER><B>%d</B></TD>\n", repinfo->count[COL_YELLOW]);
	fprintf(htmlrep, "<TD ALIGN=CENTER><B>%d</B></TD>\n", repinfo->count[COL_RED]);
	fprintf(htmlrep, "<TD ALIGN=CENTER><B>%d</B></TD>\n", repinfo->count[COL_PURPLE]);
	fprintf(htmlrep, "<TD ALIGN=CENTER><B>%d</B></TD>\n", repinfo->count[COL_CLEAR]);
	fprintf(htmlrep, "<TD ALIGN=CENTER><B>%d</B></TD>\n", repinfo->count[COL_BLUE]);
	fprintf(htmlrep, "</TR>\n");
	fprintf(htmlrep, "<TR BGCOLOR=\"#000000\">\n");
	fprintf(htmlrep, "<TD COLSPAN=6 ALIGN=CENTER>\n");
	fprintf(htmlrep, "<FONT %s><B>[Total may not equal 100%%]</B></TD> </TR>\n", getenv("MKBBCOLFONT"));

	if (strcmp(repinfo->fstate, "NOTOK") == 0) {
		fprintf(htmlrep, "<TR BGCOLOR=\"#000000\">\n");
		fprintf(htmlrep, "<TD COLSPAN=6 ALIGN=CENTER>\n");
		fprintf(htmlrep, "<FONT %s><B>[History file contains invalid entries]</B></TD></TR>\n", getenv("MKBBCOLFONT"));
	}

	fprintf(htmlrep, "</TABLE>\n");
	fprintf(htmlrep, "</CENTER>\n");

	/* Text-based report start */
	if (textrep) {
		char text_starttime[20], text_endtime[20];

		fprintf(textrep, "Availability Report\n");

		strftime(text_starttime, sizeof(text_starttime), "%b %d %Y", localtime(&st));
		strftime(text_endtime, sizeof(text_endtime), "%b %d %Y", localtime(&end));
		if (strcmp(text_starttime, text_endtime) == 0)
			fprintf(textrep, "%s\n", text_starttime);
		else
			fprintf(textrep, "%s - %s\n", text_starttime, text_endtime);

		fprintf(textrep, "\n");
		fprintf(textrep, "\n");
		fprintf(textrep, "				%s - %s\n", hostname, service);
		fprintf(textrep, "\n");
		fprintf(textrep, "				Availability:	%.2f%%\n", repinfo->availability);
		fprintf(textrep, "			Red	Yellow	Green	Purple	Clear	Blue\n");
		fprintf(textrep, "			%.2f%%	%.2f%%	%.2f%%	%.2f%%	%.2f%%	%.2f%%\n",
			repinfo->pct[COL_RED], repinfo->pct[COL_YELLOW], repinfo->pct[COL_GREEN], 
			repinfo->pct[COL_PURPLE], repinfo->pct[COL_CLEAR], repinfo->pct[COL_BLUE]);
		fprintf(textrep, "		Events	%d	%d	%d	%d	%d	%d\n",
			repinfo->count[COL_RED], repinfo->count[COL_YELLOW], repinfo->count[COL_GREEN], 
			repinfo->count[COL_PURPLE], repinfo->count[COL_CLEAR], repinfo->count[COL_BLUE]);
		fprintf(textrep, "\n");
		fprintf(textrep, "\n");
		fprintf(textrep, "				Event logs for the given period\n");
		fprintf(textrep, "\n");
		fprintf(textrep, "Event Start			Event End			Status	Duration	(Seconds)	Cause\n");
		fprintf(textrep, "\n");
		fprintf(textrep, "\n");
	}


	fprintf(htmlrep, "<BR><BR>\n");


	fprintf(htmlrep, "<CENTER>\n");
	fprintf(htmlrep, "<TABLE BORDER=0 BGCOLOR=\"#333333\" CELLSPACING=3>\n");
	fprintf(htmlrep, "<TR>\n");
	fprintf(htmlrep, "<TD COLSPAN=5><CENTER>Event logs for the given period</CENTER></TD>\n");
	fprintf(htmlrep, "</TR>\n");
	fprintf(htmlrep, "<TR BGCOLOR=\"#333333\">\n");
	fprintf(htmlrep, "<TD ALIGN=CENTER><FONT %s><B>Event Start</B></TD>\n", getenv("MKBBCOLFONT"));
	fprintf(htmlrep, "<TD ALIGN=CENTER><FONT %s><B>Event End</B></TD>\n", getenv("MKBBCOLFONT"));
	fprintf(htmlrep, "<TD ALIGN=CENTER><FONT %s><B>Status</B></TD>\n", getenv("MKBBCOLFONT"));
	fprintf(htmlrep, "<TD ALIGN=CENTER><FONT %s><B>Duration</B></TD>\n", getenv("MKBBCOLFONT"));
	fprintf(htmlrep, "<TD ALIGN=CENTER><FONT %s><B>Cause</B></TD></TR>\n", getenv("MKBBCOLFONT"));
	fprintf(htmlrep, "</TR>\n");

	for (walk = reploghead; (walk); walk = walk->next) {
		int wanted = 0;

		switch (style) {
		  case STYLE_CRIT: wanted = (walk->color == COL_RED); break;
		  case STYLE_NONGR: wanted = (walk->color != COL_GREEN); break;
		  case STYLE_OTHER: wanted = 1;
		}

		if (wanted) {
			char start[30];
			char end[30];
			time_t endtime;

			strftime(start, sizeof(start), "%a %b %d %H:%M:%S %Y", localtime(&walk->starttime));
			endtime = walk->starttime + walk->duration;
			strftime(end, sizeof(end), "%a %b %d %H:%M:%S %Y", localtime(&endtime));

			fprintf(htmlrep, "<TR BGCOLOR=%s>\n", bgcols[curbg]); curbg = (1-curbg);
			fprintf(htmlrep, "<TD ALIGN=LEFT NOWRAP>%s</TD>\n", start);
			fprintf(htmlrep, "<TD ALIGN=RIGHT NOWRAP>%s</TD>\n", end);
			fprintf(htmlrep, "<TD ALIGN=CENTER BGCOLOR=\"#000000\">");
			fprintf(htmlrep, "<A HREF=\"%s/bb-histlog.sh?HOST=%s&SERVICE=%s&TIMEBUF=%s\">", 
				getenv("CGIBINURL"), hostname, service, walk->timespec);
			fprintf(htmlrep, "<IMG SRC=\"%s/%s\" ALT=\"%s\" HEIGHT=%s WIDTH=%s BORDER=0>", 
				getenv("BBSKIN"), dotgiffilename(walk->color, 0, 1), colorname(walk->color),
				getenv("DOTHEIGHT"), getenv("DOTWIDTH"));
			fprintf(htmlrep, "</TD>\n");

			fprintf(htmlrep, "<TD ALIGN=CENTER>%s</TD>\n", durationstr(walk->duration));
			fprintf(htmlrep, "<TD>%s</TD>\n", walk->cause);
			fprintf(htmlrep, "</TR>\n\n");


			/* And the text-report */
			if (textrep) {
				fprintf(textrep, "%s	%s	%s	%s		%lu		",
					start, end, colorname(walk->color), durationstr(walk->duration), walk->duration);
				if (walk->cause) {
					char *p;

                        		for (p=walk->cause; (p && *p); ) {
						if (*p == '<') {
							p = strchr(p, '>');
							if (p) p++;
						}
						else if (*p != '\n') {
							fprintf(textrep, "%c", *p);
							p++;
						}
						else p++;
					}
					fprintf(textrep, "\n");
				}
			}
		}
	}

	fprintf(htmlrep, "<TR><TD ALIGN=RIGHT BGCOLOR=\"#000033\" COLSPAN=3>\n");
	fprintf(htmlrep, "<B>Time Critical/Offline:</B></TD>\n");
	fprintf(htmlrep, "<TD ALIGN=LEFT NOWRAP COLSPAN=2>%s</TD></TR>\n", 
		durationstr(repinfo->totduration[COL_RED]));

	if (style != STYLE_CRIT) {
		fprintf(htmlrep, "<TR><TD ALIGN=RIGHT BGCOLOR=\"#000033\" COLSPAN=3>\n");
		fprintf(htmlrep, "<B>Time Non-Critical:</B></TD>\n");
		fprintf(htmlrep, "<TD ALIGN=LEFT NOWRAP COLSPAN=2>%s</TD></TR>\n", 
			durationstr(repinfo->totduration[COL_YELLOW] + repinfo->totduration[COL_PURPLE] +
				    repinfo->totduration[COL_CLEAR]  + repinfo->totduration[COL_BLUE]));
	}

	/* And the text report ... */
	if (textrep) {
		fprintf(textrep, "\n");
		fprintf(textrep, "\n");
		fprintf(textrep, "				%s %s	(%lu secs)\n",
			"Time Critical/Offline:", durationstr(repinfo->totduration[COL_RED]), repinfo->totduration[COL_RED]);

		if (style != STYLE_CRIT) {
			fprintf(textrep, "\n");
			fprintf(textrep, "\n");
			fprintf(textrep, "				%s %s	(%lu secs)\n",
				"Time Non-Critical:", 
				durationstr(repinfo->totduration[COL_YELLOW] + repinfo->totduration[COL_PURPLE] +
					    repinfo->totduration[COL_CLEAR]  + repinfo->totduration[COL_BLUE]),
				(repinfo->totduration[COL_YELLOW] + repinfo->totduration[COL_PURPLE] + 
				 repinfo->totduration[COL_CLEAR] + repinfo->totduration[COL_BLUE]));
		}
	}

	fprintf(htmlrep, "</TABLE>\n");


	fprintf(htmlrep, "<BR><BR>\n");
	fprintf(htmlrep, "<BR><BR><CENTER><FONT COLOR=yellow>\n");
	fprintf(htmlrep, "<A HREF=\"%s\">Click here for text-based availability report</A>\n", textrepurl);
	fprintf(htmlrep, "</FONT></CENTER><BR><BR>\n");

	/* BBREPEXT extensions */
	do_bbext(htmlrep, "BBREPEXT", "rep");

	fprintf(htmlrep, "</CENTER>\n");

	headfoot(htmlrep, "replog", "", "footer", color);
}


#ifdef CGI

/*
 * This program is invoked via CGI with QUERY_STRING containing:
 *
 *	HOSTSVC=www,sample,com.conn
 *	IP=12.34.56.78
 *	COLOR=yellow
 *	PCT=98.92
 *	ST=1028810893
 *	END=1054418399
 *	RED=1.08
 *	YEL=0.00
 *	GRE=98.34
 *	PUR=0.13
 *	CLE=0.00
 *	BLU=0.45
 *	STYLE=crit
 *	FSTATE=OK
 *	REDCNT=124
 *	YELCNT=0
 *	GRECNT=153
 *	PURCNT=5
 *	CLECNT=1
 *	BLUCNT=24
 *
 */

/* These are dummy vars needed by stuff in util.c */
hostlist_t      *hosthead = NULL;
link_t          *linkhead = NULL;
link_t  null_link = { "", "", "", NULL };

/* These are needed, but not actually used */
double reportgreenlevel = 99.995;
double reportwarnlevel = 98.0;

char *hostname = "";
char *ip = "";
char *service = "";
time_t st, end;
int style;
int color;

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
	else query = malcop(getenv("QUERY_STRING"));

	token = strtok(query, "&");
	while (token) {
		char *val;
		
		val = strchr(token, '='); if (val) { *val = '\0'; val++; }

		if (argnmatch(token, "HOSTSVC")) {
			char *p = strrchr(val, '.');

			if (p) { *p = '\0'; service = malcop(p+1); }
			hostname = malcop(val);
			while ((p = strchr(hostname, ','))) *p = '.';
		}
		else if (argnmatch(token, "IP")) {
			ip = malcop(val);
		}
		else if (argnmatch(token, "STYLE")) {
			if (strcmp(val, "crit") == 0) style = STYLE_CRIT;
			else if (strcmp(val, "nongr") == 0) style = STYLE_NONGR;
			else style = STYLE_OTHER;
		}
		else if (argnmatch(token, "ST")) {
			/* Must be after "STYLE" */
			st = atol(val);
		}
		else if (argnmatch(token, "END")) {
			end = atol(val);
		}
		else if (argnmatch(token, "COLOR")) {
			char *colstr = malloc(strlen(val)+2);
			sprintf(colstr, "%s ", val);
			color = parse_color(colstr);
			free(colstr);
		}

		token = strtok(NULL, "&");
	}

	free(query);
}

int main(int argc, char *argv[])
{
	char histlogfn[MAX_PATH];
	FILE *fd;
	char textrepfullfn[MAX_PATH], textrepfn[1024], textrepurl[MAX_PATH];
	FILE *textrep;
	reportinfo_t repinfo;

	envcheck(reqenv);
	parse_query();

	sprintf(histlogfn, "%s/%s.%s", getenv("BBHIST"), commafy(hostname), service);
	fd = fopen(histlogfn, "r");
	if (fd == NULL) {
		errormsg("Cannot open history file");
	}

	parse_historyfile(fd, &repinfo, hostname, service, st, end, 0);
	fclose(fd);

	sprintf(textrepfn, "avail-%s-%s-%lu-%u.txt", hostname, service, time(NULL), (int)getpid());
	sprintf(textrepfullfn, "%s/%s", getenv("BBREP"), textrepfn);
	sprintf(textrepurl, "%s/%s", getenv("BBREPURL"), textrepfn);
	textrep = fopen(textrepfullfn, "w");

	/* Now generate the webpage */
	printf("Content-Type: text/html\n\n");

	generate_replog(stdout, textrep, textrepurl, 
			hostname, ip, service, color, style, st, end, 
			reportwarnlevel, reportgreenlevel, &repinfo);

	if (textrep) fclose(textrep);
	return 0;
}

#endif

