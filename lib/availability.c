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

static char rcsid[] = "$Id: availability.c,v 1.6 2003-06-20 13:06:16 henrik Exp $";

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>

#include "bbgen.h"
#include "debug.h"
#include "util.h"
#include "reportdata.h"

replog_t *reploghead = NULL;

char *parse_histlogfile(char *hostname, char *servicename, char *timespec)
{
	char fn[MAX_PATH];
	char *p;
	FILE *fd;
	char l[MAX_LINE_LEN];
	char cause[MAX_LINE_LEN];

	cause[0] = '\0';

	sprintf(fn, "%s/%s", getenv("BBHISTLOGS"), commafy(hostname));
	for (p = strrchr(fn, '/'); (*p); p++) if (*p == ',') *p = '_';
	sprintf(p, "/%s/%s", servicename, timespec);

	dprintf("Looking at history logfile %s\n", fn);
	fd = fopen(fn, "r");
	while (fgets(l, sizeof(l), fd)) {
		p = strchr(l, '\n'); if (p) *p = '\0';

		if ((l[0] == '&') && (strncmp(l, "&green", 6) != 0)) {
			p = skipwhitespace(skipword(l));
			strcat(cause, p);
			strcat(cause, "<BR>\n");
		}
	}

	if (strlen(cause) == 0) {
		int offset;
		rewind(fd);
		if (fgets(l, sizeof(l), fd)) {
			p = strchr(l, '\n'); if (p) *p = '\0';
			sscanf(l, "%*s %*s %*s %*s %*s %*s %*s %n", &offset);
			strncpy(cause, l+offset, sizeof(cause));
			cause[sizeof(cause)-1] = '\0';
		}
	}

	fclose(fd);
	return malcop(cause);
}

int scan_historyfile(FILE *fd, time_t fromtime, time_t totime,
		char *buf, size_t bufsize, 
		time_t *starttime, time_t *duration, char *colstr)
{
	time_t start, dur;
	int scanres;
	int err = 0;

	/*
	 * Format of history entries:
	 *    asctime-stamp newcolor starttime [duration]
	 */

	/* Is start of history after our report-end time ? */
	rewind(fd);
	fgets(buf, bufsize, fd);
	if (sscanf(buf+25, "%s %lu %lu", colstr, &start, &dur) == 2) dur = time(NULL)-start;
	if (start > totime) {
		*starttime = start;
		*duration = dur;
		strcpy(colstr, "clear");
		return 0;
	}

	/* First, do a quick scan through the file to find the approximate position where we should start */
	while ((start+dur) < fromtime) {
		if (fgets(buf, bufsize, fd)) {
			scanres = sscanf(buf+25, "%s %lu %lu", colstr, &start, &dur);
			if (scanres == 2) dur = time(NULL) - start;

			if (scanres >= 2) {
				dprintf("Skipped to entry starting %lu\n", start);

				if ((start + dur) < fromtime) {
					fseek(fd, 2048, SEEK_CUR);
					fgets(buf, bufsize, fd); /* Skip partial line */
				}
			}
			else {
				err++;
				dprintf("Bad line in history file '%s'\n", buf);
				start = dur = 0; /* Try next line */
			}
		}
		else {
			start = time(NULL);
			dur = 0;
		}
	};

	/* We know the start position of the logfile is between current pos and (current-~2048 bytes) */
	if (ftell(fd) < 2300)
		rewind(fd);
	else {
		fseek(fd, -2300, SEEK_CUR); 
		fgets(buf, bufsize, fd); /* Skip partial line */
	}

	/* Read one line at a time until we hit start of our report period */
	do {
		if (fgets(buf, bufsize, fd)) {
			scanres = sscanf(buf+25, "%s %lu %lu", colstr, &start, &dur);
			if (scanres == 2) dur = time(NULL) - start;

			if (scanres < 2) {
				err++;
				dprintf("Bad line in history file '%s'\n", buf);
				start = dur = 0; /* Try next line */
			}
			else {
				dprintf("Got entry starting %lu lasting %lu\n", start, dur);
			}
		}
		else {
			start = time(NULL);
			dur = 0;
		}
	} while ((start+dur) < fromtime);

	dprintf("Reporting starts with this entry: %s\n\n", buf);

	*starttime = start;
	*duration = dur;
	return err;
}

int parse_historyfile(FILE *fd, reportinfo_t *repinfo, char *hostname, char *servicename, time_t fromtime, time_t totime)
{
	char l[MAX_LINE_LEN];
	time_t starttime, duration;
	char colstr[MAX_LINE_LEN];
	int color, done, i, scanres;
	int fileerrors;

	for (i=0; (i<COL_COUNT); i++) {
		repinfo->totduration[i] = 0;
		repinfo->count[i] = 0;
		repinfo->pct[i] = 0.0;
	}
	repinfo->availability = 0.0;
	repinfo->fstate = "OK";
	repinfo->reportstart = time(NULL);

	fileerrors = scan_historyfile(fd, fromtime, totime, 
				      l, sizeof(l), &starttime, &duration, colstr);

	if (starttime > totime) {
		repinfo->availability = 100.0;
		repinfo->pct[COL_CLEAR] = 100.0;
		repinfo->count[COL_CLEAR] = 1;
		return COL_CLEAR;
	}

	/* If event starts before our fromtime, adjust starttime and duration */
	if (starttime < fromtime) {
		duration -= (fromtime - starttime);
		starttime = fromtime;
	}
	repinfo->reportstart = starttime;

	done = 0;
	do {
		/* If event ends after our reportend, adjust duration */
		if ((starttime + duration) > totime) duration = (totime - starttime);
		strcat(colstr, " "); color = parse_color(colstr);

		if (color != -1) {
			dprintf("In-range entry starting %lu lasting %lu color %d: %s", starttime, duration, color, l);
			repinfo->count[color]++;
			repinfo->totduration[color] += duration;

			if ((hostname != NULL) && (servicename != NULL)) {
				replog_t *newentry;
				char timecopy[26], timespec[26];
				char *token;

				/* Compute the timespec string used as the name of the historical logfile */
				strncpy(timecopy, l, 25);
				timecopy[25] = '\0';

				token = strtok(timecopy, " ");
				strcpy(timespec, token);

				for (i=1; i<5; i++) {
					strcat(timespec, "_");
					token = strtok(NULL, " ");
					strcat(timespec, token);
				}

				newentry = malloc(sizeof(replog_t));
				newentry->starttime = starttime;
				newentry->duration = duration;
				newentry->color = color;
				newentry->cause = parse_histlogfile(hostname, servicename, timespec);
				newentry->timespec = malcop(timespec);
				newentry->next = reploghead;
				reploghead = newentry;
			}
		}

		if ((starttime + duration) < totime) {
			fgets(l, sizeof(l), fd);
			scanres = sscanf(l+25, "%s %lu %lu", colstr, &starttime, &duration);
			if (scanres == 2) duration = time(NULL) - starttime;
		}
		else done = 1;
	} while (!done);

	for (i=0; (i<COL_COUNT); i++) {
		dprintf("Duration for color %d: %lu\n", i, repinfo->totduration[i]);
		repinfo->pct[i] = (100.0*repinfo->totduration[i] / (totime - repinfo->reportstart));
	}
	repinfo->availability = 100.0 - repinfo->pct[COL_RED];

	if (repinfo->availability > reportpaniclevel) color = COL_GREEN;
	else if (repinfo->availability >= reportwarnlevel) color = COL_YELLOW;
	else color = COL_RED;

	if (fileerrors) repinfo->fstate = "NOTOK";
	return color;
}


#ifdef STANDALONE

time_t reportstart, reportend;
double reportpaniclevel = 99.995;
double reportwarnlevel = 98.0;

/* These are dummy vars needed by stuff in util.c */
hostlist_t      *hosthead = NULL;
link_t          *linkhead = NULL;
link_t  null_link = { "", "", "", NULL };

int main(int argc, char *argv[])
{
	FILE *fd;
	reportinfo_t repinfo;
	int i, color;
	char *p, *hostsvc, *host, *svc;
	replog_t *rwalk;

	debug=1;

	fd = fopen(argv[1], "r");
	if (fd == NULL) { printf("Cannot open %s\n", argv[1]); exit(1); }

	reportstart = atol(argv[2]);
	reportend = atol(argv[3]);

	hostsvc = malcop(argv[1]);
	p = strrchr(hostsvc, '.');
	*p = '\0'; svc = p+1;
	p = strrchr(hostsvc, '/'); host = p+1;
	while ((p = strchr(host, ','))) *p = '.';

	color = parse_historyfile(fd, &repinfo, host, svc, reportstart, reportend);

	for (i=0; (i<COL_COUNT); i++) {
		dprintf("Color %d: Count=%d, pct=%.2f\n", i, repinfo.count[i], repinfo.pct[i]);
	}
	dprintf("Availability: %.2f, color =%d\n", repinfo.availability, color);
	dprintf("History file status: %s\n", repinfo.fstate);

	fclose(fd);

	for (rwalk = reploghead; (rwalk); rwalk = rwalk->next) {
		char start[30];
		char end[30];
		char dur[30], dhelp[30];
		time_t endtime;
		time_t duration;

		strftime(start, sizeof(start), "%a %b %d %H:%M:%S %Y", localtime(&rwalk->starttime));
		endtime = rwalk->starttime + rwalk->duration;
		strftime(end, sizeof(end), "%a %b %d %H:%M:%S %Y", localtime(&endtime));

		duration = rwalk->duration;
		dur[0] = '\0';
		if (duration > 86400) {
			sprintf(dhelp, "%lu days ", (duration / 86400));
			duration %= 86400;
			strcpy(dur, dhelp);
		}
		sprintf(dhelp, "%lu:%02lu:%02lu", duration / 3600, ((duration % 3600) / 60), (duration % 60));
		strcat(dur, dhelp);

		dprintf("Start: %s, End: %s, Color: %s, Duration: %s, Cause: %s\n",
			start, end, colorname(rwalk->color), dur, rwalk->cause);
	}

	return 0;
}
#endif

