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

static char rcsid[] = "$Id: availability.c,v 1.3 2003-06-19 19:56:00 henrik Exp $";

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>

#include "bbgen.h"
#include "debug.h"
#include "util.h"
#include "reportdata.h"

int parse_historyfile(FILE *fd, reportinfo_t *repinfo)
{
	char l[MAX_LINE_LEN];
	char colstr[MAX_LINE_LEN];
	time_t starttime, duration;
	int color;
	int scanres, done, i;
	int fileerrors = 0;
	unsigned long totduration[COL_COUNT];

	for (i=0; (i<COL_COUNT); i++) {
		totduration[i] = 0;
		repinfo->count[i] = 0;
		repinfo->pct[i] = 0.0;
	}
	repinfo->availability = 0.0;
	repinfo->fstate = "OK";
	repinfo->reportstart = time(NULL);

	/*
	 * Format of history entries:
	 *    asctime-stamp newcolor starttime [duration]
	 */

	/* Is start of history after our report-end time ? */
	rewind(fd);
	fgets(l, sizeof(l), fd);
	if (sscanf(l+25, "%s %lu %lu", colstr, &starttime, &duration) == 2) duration = time(NULL)-starttime;
	if (starttime > reportend) {
		repinfo->availability = 100.0;
		repinfo->pct[COL_CLEAR] = 100.0;
		repinfo->count[COL_CLEAR] = 1;
		return COL_CLEAR;
	}


	/* First, do a quick scan through the file to find the approximate position where we should start */
	while ((starttime+duration) < reportstart) {
		if (fgets(l, sizeof(l), fd)) {
			scanres = sscanf(l+25, "%s %lu %lu", colstr, &starttime, &duration);
			if (scanres == 2) duration = time(NULL) - starttime;

			if (scanres >= 2) {
				dprintf("Skipped to entry starting %lu\n", starttime);

				if ((starttime + duration) < reportstart) {
					fseek(fd, 2048, SEEK_CUR);
					fgets(l, sizeof(l), fd); /* Skip partial line */
				}
			}
			else {
				fileerrors++;
				dprintf("Bad line in history file '%s'\n", l);
				starttime = duration = 0; /* Try next line */
			}
		}
		else {
			starttime = time(NULL);
			duration = 0;
		}
	};

	/* We know the start position of the logfile is between current pos and (current-~2048 bytes) */
	if (ftell(fd) < 2300)
		rewind(fd);
	else {
		fseek(fd, -2300, SEEK_CUR); 
		fgets(l, sizeof(l), fd); /* Skip partial line */
	}

	/* Read one line at a time until we hit start of our report period */
	do {
		if (fgets(l, sizeof(l), fd)) {
			scanres = sscanf(l+25, "%s %lu %lu", colstr, &starttime, &duration);
			if (scanres == 2) duration = time(NULL) - starttime;

			if (scanres < 2) {
				fileerrors++;
				dprintf("Bad line in history file '%s'\n", l);
				starttime = duration = 0; /* Try next line */
			}
			else {
				dprintf("Got entry starting %lu lasting %lu\n", starttime, duration);
			}
		}
		else {
			starttime = time(NULL);
			duration = 0;
		}
	} while ((starttime+duration) < reportstart);

	dprintf("Reporting starts with this entry: %s\n\n", l);

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

