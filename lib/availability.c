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

static char rcsid[] = "$Id: availability.c,v 1.4 2003-06-19 20:21:49 henrik Exp $";

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>

#include "bbgen.h"
#include "debug.h"
#include "util.h"
#include "reportdata.h"

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

int parse_historyfile(FILE *fd, reportinfo_t *repinfo)
{
	char l[MAX_LINE_LEN];
	time_t starttime, duration;
	char colstr[MAX_LINE_LEN];
	unsigned long totduration[COL_COUNT];
	int color, done, i, scanres;
	int fileerrors;

	for (i=0; (i<COL_COUNT); i++) {
		totduration[i] = 0;
		repinfo->count[i] = 0;
		repinfo->pct[i] = 0.0;
	}
	repinfo->availability = 0.0;
	repinfo->fstate = "OK";
	repinfo->reportstart = time(NULL);

	fileerrors = scan_historyfile(fd, reportstart, reportend, 
				      l, sizeof(l), &starttime, &duration, colstr);

	if (starttime > reportend) {
		repinfo->availability = 100.0;
		repinfo->pct[COL_CLEAR] = 100.0;
		repinfo->count[COL_CLEAR] = 1;
		return COL_CLEAR;
	}

	/* If event starts before our reportstart, adjust starttime and duration */
	if (starttime < reportstart) {
		duration -= (reportstart - starttime);
		starttime = reportstart;
	}
	repinfo->reportstart = starttime;

	done = 0;
	do {
		/* If event ends after our reportend, adjust duration */
		if ((starttime + duration) > reportend) duration = (reportend - starttime);
		strcat(colstr, " "); color = parse_color(colstr);

		if (color != -1) {
			dprintf("In-range entry starting %lu lasting %lu color %d: %s", starttime, duration, color, l);
			repinfo->count[color]++;
			totduration[color] += duration;
		}

		if ((starttime + duration) < reportend) {
			fgets(l, sizeof(l), fd);
			scanres = sscanf(l+25, "%s %lu %lu", colstr, &starttime, &duration);
			if (scanres == 2) duration = time(NULL) - starttime;
		}
		else done = 1;
	} while (!done);

	for (i=0; (i<COL_COUNT); i++) {
		dprintf("Duration for color %d: %lu\n", i, totduration[i]);
		repinfo->pct[i] = (100.0*totduration[i] / (reportend - repinfo->reportstart));
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

	debug=1;

	fd = fopen(argv[1], "r");
	if (fd == NULL) { printf("Cannot open %s\n", argv[1]); exit(1); }

	reportstart = atol(argv[2]);
	reportend = atol(argv[3]);

	color = parse_historyfile(fd, &repinfo);

	for (i=0; (i<COL_COUNT); i++) {
		dprintf("Color %d: Count=%d, pct=%.2f\n", i, repinfo.count[i], repinfo.pct[i]);
	}
	dprintf("Availability: %.2f, color =%d\n", repinfo.availability, color);
	dprintf("History file status: %s\n", repinfo.fstate);

	fclose(fd);

	return 0;
}
#endif

