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

static char rcsid[] = "$Id: bb-replog.c,v 1.19 2003-08-12 21:16:05 henrik Exp $";

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
	fprintf(htmlrep, "<BR><FONT %s><B>%s - %s</B></FONT>\n", getenv("MKBBROWFONT"), hostname, service);
	fprintf(htmlrep, "<TABLE BORDER=0 BGCOLOR=\"#333333\" CELLPADDING=3>\n");
	fprintf(htmlrep, "<TR>\n");

	if (repinfo->withreport) {
		fprintf(htmlrep, "<TD COLSPAN=3><CENTER><BR><B>Availability (24x7): %.2f%%</B></CENTER></TD>\n", repinfo->fullavailability);
		fprintf(htmlrep, "<TD>&nbsp;</TD>\n");
		fprintf(htmlrep, "<TD COLSPAN=3><CENTER><B>Availability (SLA): %.2f%%</B></CENTER></TD>\n", repinfo->reportavailability);
	}
	else {
		fprintf(htmlrep, "<TD COLSPAN=7><CENTER><B><BR>Availability: %.2f%%</B></CENTER></TD>\n", repinfo->fullavailability);
	}
	fprintf(htmlrep, "</TR>\n");

	fprintf(htmlrep, "<TR BGCOLOR=\"#000033\">\n");
	fprintf(htmlrep, "<TD>&nbsp;</TD>\n");
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
	fprintf(htmlrep, "<TD ALIGN=LEFT><B>24x7</B></TD>\n");
	fprintf(htmlrep, "<TD ALIGN=CENTER><B>%.2f%%</B></TD>\n", repinfo->fullpct[COL_GREEN]);
	fprintf(htmlrep, "<TD ALIGN=CENTER><B>%.2f%%</B></TD>\n", repinfo->fullpct[COL_YELLOW]);
	fprintf(htmlrep, "<TD ALIGN=CENTER><B>%.2f%%</B></TD>\n", repinfo->fullpct[COL_RED]);
	fprintf(htmlrep, "<TD ALIGN=CENTER><B>%.2f%%</B></TD>\n", repinfo->fullpct[COL_PURPLE]);
	fprintf(htmlrep, "<TD ALIGN=CENTER><B>%.2f%%</B></TD>\n", repinfo->fullpct[COL_CLEAR]);
	fprintf(htmlrep, "<TD ALIGN=CENTER><B>%.2f%%</B></TD>\n", repinfo->fullpct[COL_BLUE]);
	fprintf(htmlrep, "</TR>\n");
	if (repinfo->withreport) {
		fprintf(htmlrep, "<TR BGCOLOR=\"#000033\">\n");
		fprintf(htmlrep, "<TD ALIGN=LEFT><B>SLA (%.2f)</B></TD>\n", reportwarnlevel);
		fprintf(htmlrep, "<TD ALIGN=CENTER><B>%.2f%%</B></TD>\n", repinfo->reportpct[COL_GREEN]);
		fprintf(htmlrep, "<TD ALIGN=CENTER><B>%.2f%%</B></TD>\n", repinfo->reportpct[COL_YELLOW]);
		fprintf(htmlrep, "<TD ALIGN=CENTER><B>%.2f%%</B></TD>\n", repinfo->reportpct[COL_RED]);
		fprintf(htmlrep, "<TD ALIGN=CENTER>-</TD>\n");
		fprintf(htmlrep, "<TD ALIGN=CENTER><B>%.2f%%</B></TD>\n", repinfo->reportpct[COL_CLEAR]);
		fprintf(htmlrep, "<TD ALIGN=CENTER>-</TD>\n");
		fprintf(htmlrep, "</TR>\n");
	}
	fprintf(htmlrep, "<TR BGCOLOR=\"#000000\">\n");
	fprintf(htmlrep, "<TD ALIGN=CENTER COLSPAN=2><B>Event count</B></TD>\n");
	fprintf(htmlrep, "<TD ALIGN=CENTER><B>%d</B></TD>\n", repinfo->count[COL_YELLOW]);
	fprintf(htmlrep, "<TD ALIGN=CENTER><B>%d</B></TD>\n", repinfo->count[COL_RED]);
	fprintf(htmlrep, "<TD ALIGN=CENTER><B>%d</B></TD>\n", repinfo->count[COL_PURPLE]);
	fprintf(htmlrep, "<TD ALIGN=CENTER><B>%d</B></TD>\n", repinfo->count[COL_CLEAR]);
	fprintf(htmlrep, "<TD ALIGN=CENTER><B>%d</B></TD>\n", repinfo->count[COL_BLUE]);
	fprintf(htmlrep, "</TR>\n");
	fprintf(htmlrep, "<TR BGCOLOR=\"#000000\">\n");
	fprintf(htmlrep, "<TD COLSPAN=7 ALIGN=CENTER>\n");
	fprintf(htmlrep, "<FONT %s><B>[Total may not equal 100%%]</B></FONT></TD> </TR>\n", getenv("MKBBCOLFONT"));

	if (strcmp(repinfo->fstate, "NOTOK") == 0) {
		fprintf(htmlrep, "<TR BGCOLOR=\"#000000\">\n");
		fprintf(htmlrep, "<TD COLSPAN=7 ALIGN=CENTER>\n");
		fprintf(htmlrep, "<FONT %s><B>[History file contains invalid entries]</B></FONT></TD></TR>\n", 
			getenv("MKBBCOLFONT"));
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
		if (repinfo->withreport) {
			fprintf(textrep, "			Availability (24x7) :	%.2f%%\n", repinfo->fullavailability);
			fprintf(textrep, "			Availability (SLA)  :	%.2f%%\n", repinfo->reportavailability);
		}
		else {
			fprintf(textrep, "				Availability:	%.2f%%\n", repinfo->fullavailability);
		}
		fprintf(textrep, "			Green	Yellow	Red	Purple	Clear	Blue\n");
		fprintf(textrep, "		24x7	%.2f%%	%.2f%%	%.2f%%	%.2f%%	%.2f%%	%.2f%%\n",
			repinfo->fullpct[COL_GREEN], repinfo->fullpct[COL_YELLOW], repinfo->fullpct[COL_RED], 
			repinfo->fullpct[COL_PURPLE], repinfo->fullpct[COL_CLEAR], repinfo->fullpct[COL_BLUE]);
		if (repinfo->withreport) {
			fprintf(textrep, "		SLA	%.2f%%	%.2f%%	%.2f%%	   -  	%.2f%%	   -  \n",
				repinfo->reportpct[COL_GREEN], repinfo->reportpct[COL_YELLOW], 
				repinfo->reportpct[COL_RED], repinfo->reportpct[COL_CLEAR]);
		}
		fprintf(textrep, "		Events	%d	%d	%d	%d	%d	%d\n",
			repinfo->count[COL_GREEN], repinfo->count[COL_YELLOW], repinfo->count[COL_RED], 
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
	fprintf(htmlrep, "<TD ALIGN=CENTER><FONT %s><B>Event Start</B></FONT></TD>\n", getenv("MKBBCOLFONT"));
	fprintf(htmlrep, "<TD ALIGN=CENTER><FONT %s><B>Event End</B></FONT></TD>\n", getenv("MKBBCOLFONT"));
	fprintf(htmlrep, "<TD ALIGN=CENTER><FONT %s><B>Status</B></FONT></TD>\n", getenv("MKBBCOLFONT"));
	fprintf(htmlrep, "<TD ALIGN=CENTER><FONT %s><B>Duration</B></FONT></TD>\n", getenv("MKBBCOLFONT"));
	fprintf(htmlrep, "<TD ALIGN=CENTER><FONT %s><B>Cause</B></FONT></TD>\n", getenv("MKBBCOLFONT"));
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
			int angrygif = (repinfo->withreport && walk->affectssla);

			strftime(start, sizeof(start), "%a %b %d %H:%M:%S %Y", localtime(&walk->starttime));
			endtime = walk->starttime + walk->duration;
			strftime(end, sizeof(end), "%a %b %d %H:%M:%S %Y", localtime(&endtime));

			fprintf(htmlrep, "<TR BGCOLOR=%s>\n", bgcols[curbg]); curbg = (1-curbg);
			fprintf(htmlrep, "<TD ALIGN=LEFT NOWRAP>%s</TD>\n", start);
			fprintf(htmlrep, "<TD ALIGN=RIGHT NOWRAP>%s</TD>\n", end);
			fprintf(htmlrep, "<TD ALIGN=CENTER BGCOLOR=\"#000000\">");
			fprintf(htmlrep, "<A HREF=\"%s/bb-histlog.sh?HOST=%s&amp;SERVICE=%s&amp;TIMEBUF=%s\">", 
				getenv("CGIBINURL"), hostname, service, walk->timespec);
			fprintf(htmlrep, "<IMG SRC=\"%s/%s\" ALT=\"%s\" HEIGHT=%s WIDTH=%s BORDER=0>", 
				getenv("BBSKIN"), dotgiffilename(walk->color, 0, !angrygif), colorname(walk->color),
				getenv("DOTHEIGHT"), getenv("DOTWIDTH"));
			fprintf(htmlrep, "</A></TD>\n");

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
	fprintf(htmlrep, "<B>Time Critical/Offline (24x7):</B></TD>\n");
	fprintf(htmlrep, "<TD ALIGN=LEFT NOWRAP COLSPAN=2>%s</TD></TR>\n", 
		durationstr(repinfo->fullduration[COL_RED]));

	if (style != STYLE_CRIT) {
		fprintf(htmlrep, "<TR><TD ALIGN=RIGHT BGCOLOR=\"#000033\" COLSPAN=3>\n");
		fprintf(htmlrep, "<B>Time Non-Critical (24x7):</B></TD>\n");
		fprintf(htmlrep, "<TD ALIGN=LEFT NOWRAP COLSPAN=2>%s</TD></TR>\n", 
			durationstr(repinfo->fullduration[COL_YELLOW] + repinfo->fullduration[COL_PURPLE] +
				    repinfo->fullduration[COL_CLEAR]  + repinfo->fullduration[COL_BLUE]));
	}

	if (repinfo->withreport) {
		fprintf(htmlrep, "<TR><TD ALIGN=RIGHT BGCOLOR=\"#000033\" COLSPAN=3>\n");
		fprintf(htmlrep, "<B>Time Critical/Offline (SLA):</B></TD>\n");
		fprintf(htmlrep, "<TD ALIGN=LEFT NOWRAP COLSPAN=2>%s</TD></TR>\n", 
			durationstr(repinfo->reportduration[COL_RED]));

		if (style != STYLE_CRIT) {
			fprintf(htmlrep, "<TR><TD ALIGN=RIGHT BGCOLOR=\"#000033\" COLSPAN=3>\n");
			fprintf(htmlrep, "<B>Time Non-Critical (SLA):</B></TD>\n");
			fprintf(htmlrep, "<TD ALIGN=LEFT NOWRAP COLSPAN=2>%s</TD></TR>\n", 
				durationstr(repinfo->reportduration[COL_YELLOW]));
		}
	}


	/* And the text report ... */
	if (textrep) {
		fprintf(textrep, "\n");
		fprintf(textrep, "\n");
		fprintf(textrep, "			%s %s	(%lu secs)\n",
			"Time Critical/Offline (24x7):", durationstr(repinfo->fullduration[COL_RED]), repinfo->fullduration[COL_RED]);

		if (style != STYLE_CRIT) {
			fprintf(textrep, "			%s %s	(%lu secs)\n",
				"Time Non-Critical (24x7):", 
				durationstr(repinfo->fullduration[COL_YELLOW] + repinfo->fullduration[COL_PURPLE] +
					    repinfo->fullduration[COL_CLEAR]  + repinfo->fullduration[COL_BLUE]),
				(repinfo->fullduration[COL_YELLOW] + repinfo->fullduration[COL_PURPLE] + 
				 repinfo->fullduration[COL_CLEAR] + repinfo->fullduration[COL_BLUE]));
		}


		if (repinfo->withreport) {
			fprintf(textrep, "\n");
			fprintf(textrep, "\n");
			fprintf(textrep, "			%s %s	(%lu secs)\n",
				"Time Critical/Offline (SLA) :", durationstr(repinfo->reportduration[COL_RED]), repinfo->reportduration[COL_RED]);

			if (style != STYLE_CRIT) {
				fprintf(textrep, "			%s %s	(%lu secs)\n",
					"Time Non-Critical (SLA) :", 
					durationstr(repinfo->reportduration[COL_YELLOW]), repinfo->fullduration[COL_YELLOW]);
			}
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
 *	REPORTTIME=*:HHHH:MMMM
 *	COLOR=yellow
 *	WARNPCT=98.5
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
char *reporttime = NULL;
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
	else query = urldecode("QUERY_STRING");

	if (!urlvalidate(query, NULL)) {
		errormsg("Invalid request");
		return;
	}

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
		else if (argnmatch(token, "REPORTTIME")) {
			reporttime = (char *) malloc(strlen(val)+strlen("REPORTTIME=")+1);
			sprintf(reporttime, "REPORTTIME=%s", val);
		}
		else if (argnmatch(token, "WARNPCT")) {
			reportwarnlevel = atof(val);
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
			char *colstr = (char *) malloc(strlen(val)+2);
			sprintf(colstr, "%s ", val);
			color = parse_color(colstr);
			free(colstr);
		}
		else if (argnmatch(token, "RECENTGIFS")) {
			use_recentgifs = atoi(val);
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

	color = parse_historyfile(fd, &repinfo, hostname, service, st, end, 0, reportwarnlevel, reportgreenlevel, reporttime);
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

