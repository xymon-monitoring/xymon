/*----------------------------------------------------------------------------*/
/* Hobbit RRD handler module.                                                 */
/*                                                                            */
/* Copyright (C) 2004 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char disk_rcsid[] = "$Id: do_disk.c,v 1.15 2005-03-17 21:08:15 henrik Exp $";

static char *disk_params[] = { "rrdcreate", rrdfn, "DS:pct:GAUGE:600:0:100", "DS:used:GAUGE:600:0:U", 
				rra1, rra2, rra3, rra4, NULL };

/* This is ported almost directly from disk-larrd.pl */

int do_disk_larrd(char *hostname, char *testname, char *msg, time_t tstamp)
{
	enum { DT_IRIX, DT_AS400, DT_NT, DT_UNIX, DT_NETAPP } dsystype;
	char *eoln, *curline;

	if (strstr(msg, " xfs ") || strstr(msg, " efs ") || strstr(msg, " cxfs ")) dsystype = DT_IRIX;
	else if (strstr(msg, "DASD")) dsystype = DT_AS400;
	else if (strstr(msg, "NetAPP")) dsystype = DT_NETAPP;
	else if (strstr(msg, "Filesystem")) dsystype = DT_NT;
	else dsystype = DT_UNIX;

	curline = msg; eoln = strchr(curline, '\n');
	while (eoln) {
		char *fsline, *p;
		char *columns[20];
		int i;
		char *diskname = NULL;
		int pused = -1;
		unsigned long long aused = 0;

		curline = eoln+1; eoln = strchr(curline, '\n'); if (eoln) *eoln = '\0';

		/* AS/400 reports must contain the word DASD */
		if ((dsystype == DT_AS400) && (strstr(curline, "DASD") == NULL)) continue;

		/* All clients except AS/400 report the mount-point with slashes - ALSO Win32 clients. */
		if ((dsystype != DT_AS400) && (strchr(curline, '/') == NULL)) continue;

		if ((dsystype != DT_NETAPP) && (*curline == '&')) continue; /* red/yellow filesystems show up twice */
		if ((dsystype != DT_NETAPP) && (strstr(curline, " red ") || strstr(curline, " yellow "))) continue;

		for (i=0; (i<20); i++) columns[i] = "";
		fsline = xstrdup(curline); i = 0; p = strtok(fsline, " ");
		while (p && (i < 20)) { columns[i++] = p; p = strtok(NULL, " "); }

		/* 
		 * Some Unix filesystem reports contain the word "Filesystem".
		 * So check if there's a slash in the NT filesystem letter - if yes,
		 * then it's really a Unix system after all.
		 */
		if ((dsystype == DT_NT) && strchr(columns[0], '/') && strlen(columns[5])) dsystype = DT_UNIX;

		switch (dsystype) {
		  case DT_IRIX:
			diskname = xstrdup(columns[6]);
			p = diskname; while ((p = strchr(p, '/')) != NULL) { *p = ','; }
			p = strchr(columns[5], '%'); if (p) *p = ' ';
			pused = atoi(columns[5]);
			aused = atoi(columns[3]);
			break;
		  case DT_AS400:
			diskname = xstrdup(",DASD");
			p = strchr(columns[12], '%'); if (p) *p = ' ';
			pused = atoi(columns[12]);
			aused = 0; /* Not available */
			break;
		  case DT_NT:
			diskname = xmalloc(strlen(columns[0])+2);
			sprintf(diskname, ",%s", columns[0]);
			p = strchr(columns[4], '%'); if (p) *p = ' ';
			pused = atoi(columns[4]);
			aused = atoi(columns[2]);
			break;
		  case DT_UNIX:
			diskname = xstrdup(columns[5]);
			p = diskname; while ((p = strchr(p, '/')) != NULL) { *p = ','; }
			p = strchr(columns[4], '%'); if (p) *p = ' ';
			pused = atoi(columns[4]);
			aused = atoi(columns[2]);
			break;
		  case DT_NETAPP:
			diskname = xstrdup(columns[1]);
			p = diskname; while ((p = strchr(p, '/')) != NULL) { *p = ','; }
			pused = atoi(columns[5]);
			p = columns[3] + strspn(columns[3], "0123456789");
			aused = atoll(columns[3]);
			/* Convert to KB if there's a modifier after the numbers */
			if (*p == 'M') aused *= 1024;
			else if (*p == 'G') aused *= (1024*1024);
			else if (*p == 'T') aused *= (1024*1024*1024);
			break;
		}

		if (diskname && (pused != -1)) {
			if (strcmp(diskname, ",") == 0) {
				diskname = xrealloc(diskname, 6);
				strcpy(diskname, ",root");
			}

			/* 
			 * Use testname here. 
			 * The disk-handler also gets data from NetAPP inode- and qtree-messages,
			 * that are virtually identical to the disk-messages. So lets just handle
			 * all of it by using the testname as part of the filename.
			 */
			sprintf(rrdfn, "%s%s.rrd", testname, diskname);
			sprintf(rrdvalues, "%d:%d:%llu", (int)tstamp, pused, aused);
			create_and_update_rrd(hostname, rrdfn, disk_params, update_params);
		}
		if (diskname) { xfree(diskname); diskname = NULL; }

		if (eoln) *eoln = '\n';
		xfree(fsline);
	}

	return 0;
}

