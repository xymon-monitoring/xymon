/*----------------------------------------------------------------------------*/
/* Hobbit overview webpage generator tool.                                    */
/*                                                                            */
/* Copyright (C) 2002-2006 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "csvreport.h"
#include "util.h"

void csv_availability(char *fn, char csvdelim)
{
	hostlist_t *hl;
	FILE *fd;

	char *header = "Hostname,Testname,Start,End,24x7 availability %,24x7 green %,24x7 green secs,24x7 clear %,24x7 clear secs,24x7 blue %,24x7 blue secs,24x7 purple %,24x7 purple secs,24x7 yellow %,24x7 yellow secs,24x7 red %,24x7 red secs,SLA availability %,SLA green %,SLA green secs,SLA clear %,SLA clear secs,SLA blue %,SLA blue secs,SLA purple %,SLA purple secs,SLA yellow %,SLA yellow secs,SLA red %,SLA red secs";

	fd = fopen(fn, "w");
	if (fd == NULL) {
		errprintf("Cannot open output file %s: %s\n", fn, strerror(errno));
		return;
	}

	if (csvdelim != ',') {
		char *p, *newheader;

		newheader = strdup(header);
		p = newheader; while ((p = strchr(p, ',')) != NULL) *p = csvdelim;
		header = newheader;
	}
	fprintf(fd, "%s\n", header);

	for (hl = hostlistBegin(); (hl); hl = hostlistNext()) {
		host_t *hwalk = hl->hostentry;
		entry_t *ewalk;
		int i;

		for (ewalk = hwalk->entries; (ewalk); ewalk = ewalk->next) {
			fprintf(fd, "%s%c%s%c%ld%c%ld", 
				hwalk->hostname, 
				csvdelim, ewalk->column->name, 
				csvdelim, ewalk->repinfo->reportstart, 
				csvdelim, reportend);

			fprintf(fd, "%c%.2f", 
				csvdelim, ewalk->repinfo->fullavailability);
			for (i=0; (i<COL_COUNT); i++) {
				fprintf(fd, "%c%.2f%c%ld", 
					csvdelim, ewalk->repinfo->fullpct[i],
					csvdelim, ewalk->repinfo->fullduration[i]);
			}

			fprintf(fd, "%c%.2f", 
				csvdelim, ewalk->repinfo->reportavailability);
			for (i=0; (i<COL_COUNT); i++) {
				fprintf(fd, "%c%.2f%c%ld", 
					csvdelim, ewalk->repinfo->reportpct[i],
					csvdelim, ewalk->repinfo->reportduration[i]);
			}
			fprintf(fd, "\n");
		}
	}

	fclose(fd);
}

