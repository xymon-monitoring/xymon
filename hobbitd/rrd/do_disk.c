/*----------------------------------------------------------------------------*/
/* Big Brother message daemon.                                                */
/*                                                                            */
/* Copyright (C) 2004 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char disk_rcsid[] = "$Id: do_disk.c,v 1.6 2004-11-24 12:50:51 henrik Exp $";

static char *disk_params[] = { "rrdcreate", rrdfn, "DS:pct:GAUGE:600:0:100", "DS:used:GAUGE:600:0:U", 
				rra1, rra2, rra3, rra4, NULL };

/* This is ported almost directly from disk-larrd.pl */

int do_disk_larrd(char *hostname, char *testname, char *msg, time_t tstamp)
{
	enum { DT_IRIX, DT_AS400, DT_NT, DT_UNIX } dsystype;
	char *eoln, *curline;

	if (strstr(msg, " xfs ") || strstr(msg, " efs ") || strstr(msg, " cxfs ")) dsystype = DT_IRIX;
	else if (strstr(msg, "DASD")) dsystype = DT_AS400;
	else if (strstr(msg, "Filesystem")) dsystype = DT_NT;
	else dsystype = DT_UNIX;

	curline = msg; eoln = strchr(curline, '\n');
	while (eoln) {
		char *fsline, *p;
		char *columns[20];
		int i;
		char *diskname = NULL;
		int pused = -1;
		unsigned long aused = 0;

		curline = eoln+1; eoln = strchr(curline, '\n'); if (eoln) *eoln = '\0';

		if (*curline == '&') continue;	/* red/yellow filesystems show up twice */
		if ((strchr(curline, '/') == NULL) && (dsystype != DT_AS400)) continue;
		if ((dsystype == DT_AS400) && (strstr(curline, "DASD") == NULL)) continue;
		if (strstr(curline, "bloater")) continue;
		if (strstr(curline, " red ") || strstr(curline, " yellow ")) continue;

		for (i=0; (i<20); i++) columns[i] = "";
		fsline = strdup(curline); i = 0; p = strtok(fsline, " ");
		while (p && (i < 20)) { columns[i++] = p; p = strtok(NULL, " "); }

		switch (dsystype) {
		  case DT_IRIX:
			diskname = strdup(columns[6]);
			p = diskname; while ((p = strchr(p, '/')) != NULL) { *p = ','; }
			p = strchr(columns[5], '%'); if (p) *p = ' ';
			pused = atoi(columns[5]);
			aused = atoi(columns[3]);
			break;
		  case DT_AS400:
			diskname = strdup(",DASD");
			p = strchr(columns[12], '%'); if (p) *p = ' ';
			pused = atoi(columns[12]);
			aused = 0; /* Not available */
			break;
		  case DT_NT:
			diskname = malloc(strlen(columns[0])+2);
			sprintf(diskname, ",%s", columns[0]);
			p = strchr(columns[4], '%'); if (p) *p = ' ';
			pused = atoi(columns[4]);
			aused = atoi(columns[2]);
			break;
		  case DT_UNIX:
			diskname = strdup(columns[5]);
			p = diskname; while ((p = strchr(p, '/')) != NULL) { *p = ','; }
			p = strchr(columns[4], '%'); if (p) *p = ' ';
			pused = atoi(columns[4]);
			aused = atoi(columns[2]);
			break;
		}

		if (diskname && (pused != -1)) {
			if (strcmp(diskname, ",") == 0) {
				diskname = realloc(diskname, 6);
				strcpy(diskname, ",root");
			}

			sprintf(rrdfn, "disk%s.rrd", diskname);
			sprintf(rrdvalues, "%d:%d:%lu", (int)tstamp, pused, aused);
			create_and_update_rrd(hostname, rrdfn, disk_params, update_params);
		}
		if (diskname) { free(diskname); diskname = NULL; }

		if (eoln) *eoln = '\n';
	}

	return 0;
}

