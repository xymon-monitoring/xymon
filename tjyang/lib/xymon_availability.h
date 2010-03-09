/*----------------------------------------------------------------------------*/
/* Hobbit monitor library.                                                    */
/*                                                                            */
/* Copyright (C) 2002-2009 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __AVAILABILITY_H__
#define __AVAILABILITY_H__

#include "color.h"

typedef struct reportinfo_t {
	char *fstate;
	time_t reportstart;
	int count[COL_COUNT];

	double fullavailability;
	int fullstops;
	double fullpct[COL_COUNT];
	unsigned long fullduration[COL_COUNT];

	int withreport;
	double reportavailability;
	int reportstops;
	double reportpct[COL_COUNT];
	unsigned long reportduration[COL_COUNT];
} reportinfo_t;

typedef struct replog_t {
        time_t starttime;
        time_t duration;
        int color;
	int affectssla;
        char *cause;
	char *timespec;
        struct replog_t *next;
} replog_t;

extern replog_t *reploghead;

extern char *durationstr(time_t duration);
extern int parse_historyfile(FILE *fd, reportinfo_t *repinfo, char *hostname, char *servicename, 
				time_t fromtime, time_t totime, int for_history,
				double warnlevel, double greenlevel, int warnstops,
				char *reporttime);
extern replog_t *save_replogs(void);
extern void restore_replogs(replog_t *head);
extern int history_color(FILE *fd, time_t snapshot, time_t *starttime, char **histlogname);

#endif

