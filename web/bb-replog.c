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

static char rcsid[] = "$Id: bb-replog.c,v 1.1 2003-06-20 12:30:41 henrik Exp $";

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "bbgen.h"
#include "util.h"
#include "reportdata.h"
#include "debug.h"

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

#define STYLE_CRIT 0
#define STYLE_NONGR 1
#define STYLE_OTHER 2

char *hostname = "";
char *ip = "";
char *service = "";
time_t st, end;
int style;
reportinfo_t repinfo;
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

void errormsg(char *msg)
{
	printf("Content-type: text/html\n\n");
	printf("<html><head><title>Invalid request</title></head>\n");
	printf("<body>%s</body></html>\n", msg);
	exit(1);
}

void parse_query(void)
{
	char *query, *token;

	if (getenv("QUERY_STRING") == NULL) errormsg("Invalid request");
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


char *durationstr(time_t duration)
{
	static char dur[100];
	char dhelp[100];

	if (duration <= 0) {
		strcpy(dur, "none");
	}
	else {
		dur[0] = '\0';
		if (duration > 86400) {
			sprintf(dhelp, "%lu days ", (duration / 86400));
			duration %= 86400;
			strcpy(dur, dhelp);
		}
		sprintf(dhelp, "%lu:%02lu:%02lu", duration / 3600, ((duration % 3600) / 60), (duration % 60));
		strcat(dur, dhelp);
	}

	return dur;
}


/* These are dummy vars needed by stuff in util.c */
hostlist_t      *hosthead = NULL;
link_t          *linkhead = NULL;
link_t  null_link = { "", "", "", NULL };

/* These are needed, but not actually used */
double reportpaniclevel = 99.995;
double reportwarnlevel = 98.0;


int main(int argc, char *argv[])
{
	FILE *fd;
	char filename[MAX_PATH];
	int color;
	reportinfo_t repinfo;
	replog_t *walk;
	char *bgcols[2] = { "\"#000000\"", "\"#000033\"" };
	int curbg = 0;
	char textrepfullfn[MAX_PATH], textrepfn[1024];
	FILE *textrep;
	char text_starttime[20], text_endtime[20];

	envcheck(reqenv);

	parse_query();
	sethostenv(hostname, ip, service, colorname(color));
	sethostenv_report(st, end, reportwarnlevel, reportpaniclevel);

	sprintf(filename, "%s/%s.%s", getenv("BBHIST"), commafy(hostname), service);
	fd = fopen(filename, "r");
	color = parse_historyfile(fd, &repinfo, hostname, service, st, end);
	fclose(fd);

	sprintf(textrepfn, "avail-%s-%s-%lu-%u.txt", hostname, service, time(NULL), (int)getpid());
	sprintf(textrepfullfn, "%s/%s", getenv("BBREP"), textrepfn);
	textrep = fopen(textrepfullfn, "w");
	if (textrep == NULL) errormsg("Cannot open output file in BBREP");



	/* Now generate the webpage */
	printf("Content-Type: text/html\n\n");

	headfoot(stdout, "replog", "", "header", color);

	printf("\n");

	printf("<CENTER>\n");
	printf("<BR><FONT %s><H2>%s - %s</H2></FONT>\n", getenv("MKBBROWFONT"), hostname, service);
	printf("<TABLE BORDER=0 BGCOLOR=\"#333333\" CELLPADDING=3>\n");
	printf("<TR BGCOLOR=\"#000000\">\n");
	printf("</TR>\n");
	printf("<TR></TR>\n");
	printf("<TR>\n");
	printf("<TD COLSPAN=6><CENTER><B>Overall Availability: %.2f%%</A></CENTER></TD></TR>\n", repinfo.availability);
	printf("<TR BGCOLOR=\"#000033\">\n");
	printf("<TR BGCOLOR=\"#000000\">\n");
	printf("<TD ALIGN=CENTER><IMG SRC=\"%s/%s\" ALT=\"green\" HEIGHT=16 WIDTH=16 BORDER=0></TD>\n", getenv("BBSKIN"), dotgiffilename(COL_GREEN, 0, 1));
	printf("<TD ALIGN=CENTER><IMG SRC=\"%s/%s\" ALT=\"yellow\" HEIGHT=16 WIDTH=16 BORDER=0></TD>\n", getenv("BBSKIN"), dotgiffilename(COL_YELLOW, 0, 1));
	printf("<TD ALIGN=CENTER><IMG SRC=\"%s/%s\" ALT=\"red\" HEIGHT=16 WIDTH=16 BORDER=0></TD>\n",  getenv("BBSKIN"), dotgiffilename(COL_RED, 0, 1));
	printf("<TD ALIGN=CENTER><IMG SRC=\"%s/%s\" ALT=\"purple\" HEIGHT=16 WIDTH=16 BORDER=0></TD>\n", getenv("BBSKIN"), dotgiffilename(COL_PURPLE, 0, 1));
	printf("<TD ALIGN=CENTER><IMG SRC=\"%s/%s\" ALT=\"clear\" HEIGHT=16 WIDTH=16 BORDER=0></TD>\n", getenv("BBSKIN"), dotgiffilename(COL_CLEAR, 0, 1));
	printf("<TD ALIGN=CENTER><IMG SRC=\"%s/%s\" ALT=\"blue\" HEIGHT=16 WIDTH=16 BORDER=0></TD>\n", getenv("BBSKIN"), dotgiffilename(COL_BLUE, 0, 1));
	printf("</TR>\n");
	printf("<TR BGCOLOR=\"#000033\">\n");
	printf("<TD ALIGN=CENTER><B>%.2f%%</B></TD>\n", repinfo.pct[COL_GREEN]);
	printf("<TD ALIGN=CENTER><B>%.2f%%</B></TD>\n", repinfo.pct[COL_YELLOW]);
	printf("<TD ALIGN=CENTER><B>%.2f%%</B></TD>\n", repinfo.pct[COL_RED]);
	printf("<TD ALIGN=CENTER><B>%.2f%%</B></TD>\n", repinfo.pct[COL_PURPLE]);
	printf("<TD ALIGN=CENTER><B>%.2f%%</B></TD>\n", repinfo.pct[COL_CLEAR]);
	printf("<TD ALIGN=CENTER><B>%.2f%%</B></TD>\n", repinfo.pct[COL_BLUE]);
	printf("</TR>\n");
	printf("<TD ALIGN=CENTER><B>Event count</B></TD>\n");
	printf("<TD ALIGN=CENTER><B>%d</B></TD>\n", repinfo.count[COL_YELLOW]);
	printf("<TD ALIGN=CENTER><B>%d</B></TD>\n", repinfo.count[COL_RED]);
	printf("<TD ALIGN=CENTER><B>%d</B></TD>\n", repinfo.count[COL_PURPLE]);
	printf("<TD ALIGN=CENTER><B>%d</B></TD>\n", repinfo.count[COL_CLEAR]);
	printf("<TD ALIGN=CENTER><B>%d</B></TD>\n", repinfo.count[COL_BLUE]);
	printf("</TR>\n");
	printf("<TR BGCOLOR=\"#000000\">\n");
	printf("<TD COLSPAN=6 ALIGN=CENTER>\n");
	printf("<FONT %s><B>[Total may not equal 100%%]</B></TD> </TR>\n", getenv("MKBBCOLFONT"));

	if (strcmp(repinfo.fstate, "NOTOK") == 0) {
		printf("<TR BGCOLOR=\"#000000\">\n");
		printf("<TD COLSPAN=6 ALIGN=CENTER>\n");
		printf("<FONT %s><B>[History file contains invalid entries]</B></TD></TR>\n", getenv("MKBBCOLFONT"));
	}

	printf("</TABLE>\n");
	printf("</CENTER>\n");

	/* Text-based report start */
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
	fprintf(textrep, "				Availability:	%.2f%%\n", repinfo.availability);
	fprintf(textrep, "			Red	Yellow	Green	Purple	Clear	Blue\n");
	fprintf(textrep, "			%.2f%%	%.2f%%	%.2f%%	%.2f%%	%.2f%%	%.2f%%\n",
		repinfo.pct[COL_RED], repinfo.pct[COL_YELLOW], repinfo.pct[COL_GREEN], 
		repinfo.pct[COL_PURPLE], repinfo.pct[COL_CLEAR], repinfo.pct[COL_BLUE]);
	fprintf(textrep, "		Events	%d	%d	%d	%d	%d	%d\n",
		repinfo.count[COL_RED], repinfo.count[COL_YELLOW], repinfo.count[COL_GREEN], 
		repinfo.count[COL_PURPLE], repinfo.count[COL_CLEAR], repinfo.count[COL_BLUE]);
	fprintf(textrep, "\n");
	fprintf(textrep, "\n");
	fprintf(textrep, "				Event logs for the given period\n");
	fprintf(textrep, "\n");
	fprintf(textrep, "Event Start			Event End			Status	Duration	(Seconds)	Cause\n");
	fprintf(textrep, "\n");
	fprintf(textrep, "\n");


	printf("<BR><BR>\n");


	printf("<CENTER>\n");
	printf("<TABLE BORDER=0 BGCOLOR=\"#333333\" CELLSPACING=3>\n");
	printf("<TR>\n");
	printf("<TD COLSPAN=5><CENTER>Event logs for the given period</CENTER></TD>\n");
	printf("</TR>\n");
	printf("<TR BGCOLOR=\"#333333\">\n");
	printf("<TD ALIGN=CENTER><FONT %s><B>Event Start</B></TD>\n", getenv("MKBBCOLFONT"));
	printf("<TD ALIGN=CENTER><FONT %s><B>Event End</B></TD>\n", getenv("MKBBCOLFONT"));
	printf("<TD ALIGN=CENTER><FONT %s><B>Status</B></TD>\n", getenv("MKBBCOLFONT"));
	printf("<TD ALIGN=CENTER><FONT %s><B>Duration</B></TD>\n", getenv("MKBBCOLFONT"));
	printf("<TD ALIGN=CENTER><FONT %s><B>Cause</B></TD></TR>\n", getenv("MKBBCOLFONT"));
	printf("</TR>\n");

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

			printf("<TR BGCOLOR=%s>\n", bgcols[curbg]); curbg = (1-curbg);
			printf("<TD ALIGN=LEFT NOWRAP>%s</TD>\n", start);
			printf("<TD ALIGN=RIGHT NOWRAP>%s</TD>\n", end);
			printf("<TD ALIGN=CENTER BGCOLOR=\"#000000\">");
			printf("<A HREF=\"%s/bb-histlog.sh?HOST=%s&SERVICE=%s&TIMEBUF=%s\">", 
				getenv("CGIBINURL"), hostname, service, walk->timespec);
			printf("<IMG SRC=\"%s/%s\" ALT=\"%s\" HEIGHT=%s WIDTH=%s BORDER=0>", 
				getenv("BBSKIN"), dotgiffilename(walk->color, 0, 1), colorname(walk->color),
				getenv("DOTHEIGHT"), getenv("DOTWIDTH"));
			printf("</TD>\n");

			printf("<TD ALIGN=CENTER>%s</TD>\n", durationstr(walk->duration));
			printf("<TD>%s</TD>\n", walk->cause);
			printf("</TR>\n\n");


			/* And the text-report */
			fprintf(textrep, "%s	%s	%s	%s		%lu		%s\n",
				start, end, colorname(walk->color), durationstr(walk->duration), walk->duration, walk->cause );
		}
	}

	printf("<TR><TD ALIGN=RIGHT BGCOLOR=\"#000033\" COLSPAN=3>\n");
	printf("<B>Time Critical/Offline:</B></TD>\n");
	printf("<TD ALIGN=LEFT NOWRAP COLSPAN=2>%s</TD></TR>\n", 
		durationstr(repinfo.totduration[COL_RED]));

	if (style != STYLE_CRIT) {
		printf("<TR><TD ALIGN=RIGHT BGCOLOR=\"#000033\" COLSPAN=3>\n");
		printf("<B>Time Non-Critical:</B></TD>\n");
		printf("<TD ALIGN=LEFT NOWRAP COLSPAN=2>%s</TD></TR>\n", 
			durationstr(repinfo.totduration[COL_YELLOW] + repinfo.totduration[COL_PURPLE] +
				    repinfo.totduration[COL_CLEAR]  + repinfo.totduration[COL_BLUE]));
	}

	/* And the text report ... */
	fprintf(textrep, "\n");
	fprintf(textrep, "\n");
	fprintf(textrep, "				%s %s	(%lu secs)\n",
		"Time Critical/Offline:", durationstr(repinfo.totduration[COL_RED]), repinfo.totduration[COL_RED]);

	if (style != STYLE_CRIT) {
		fprintf(textrep, "\n");
		fprintf(textrep, "\n");
		fprintf(textrep, "				%s %s	(%lu secs)\n",
			"Time Non-Critical:", 
			durationstr(repinfo.totduration[COL_YELLOW] + repinfo.totduration[COL_PURPLE] +
				    repinfo.totduration[COL_CLEAR]  + repinfo.totduration[COL_BLUE]),
			(repinfo.totduration[COL_YELLOW] + repinfo.totduration[COL_PURPLE] + 
			 repinfo.totduration[COL_CLEAR] + repinfo.totduration[COL_BLUE]));
	}


	printf("</TABLE>\n");


	printf("<BR><BR>\n");
	printf("<BR><BR><CENTER><FONT COLOR=yellow>\n");
	printf("<A HREF=\"%s/%s\">Click here for text-based availability report</A>\n",
		getenv("BBREPURL"), textrepfn);
	printf("</FONT></CENTER><BR><BR>\n");

	/* BBREPEXT extensions */

	printf("</CENTER>\n");

	headfoot(stdout, "replog", "", "footer", color);
	fclose(textrep);

	return 0;
}

