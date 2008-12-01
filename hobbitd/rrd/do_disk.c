/*----------------------------------------------------------------------------*/
/* Hobbit RRD handler module.                                                 */
/*                                                                            */
/* Copyright (C) 2004-2006 Henrik Storner <henrik@hswn.dk>                    */
/* Copyright (C) 2006 Francesco Duranti (fduranti@q8.it) (DBCHECK, NETAPP2)   */
/* Copyright (C) 2007 Francois Lacroix (BBWIN)				      */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char disk_rcsid[] = "$Id: do_disk.c,v 1.31 2006-06-09 22:23:49 henrik Exp $";

int do_disk_rrd(char *hostname, char *testname, char *msg, time_t tstamp)
{
	static char *disk_params[] = { "rrdcreate", rrdfn, 
				       "DS:pct:GAUGE:600:0:U", /* Upper bound is U because TblSpace can grow >100 */
				       "DS:used:GAUGE:600:0:U", 
					rra1, rra2, rra3, rra4, NULL };
	static char *disk_tpl      = NULL;

	enum { DT_IRIX, DT_AS400, DT_NT, DT_UNIX, DT_NETAPP, DT_NETAPP2, DT_NETWARE, DT_BBWIN, DT_DBCHECK } dsystype;
	char *eoln, *curline;
	static int ptnsetup = 0;
	static pcre *inclpattern = NULL;
	static pcre *exclpattern = NULL;

	if (disk_tpl == NULL) disk_tpl = setup_template(disk_params);

	if (!ptnsetup) {
		const char *errmsg;
		int errofs;
		char *ptn;

		ptnsetup = 1;
		ptn = getenv("RRDDISKS");
		if (ptn && strlen(ptn)) {
			inclpattern = pcre_compile(ptn, PCRE_CASELESS, &errmsg, &errofs, NULL);
			if (!inclpattern) errprintf("PCRE compile of RRDDISKS='%s' failed, error %s, offset %d\n", 
						    ptn, errmsg, errofs);
		}
		ptn = getenv("NORRDDISKS");
		if (ptn && strlen(ptn)) {
			exclpattern = pcre_compile(ptn, PCRE_CASELESS, &errmsg, &errofs, NULL);
			if (!exclpattern) errprintf("PCRE compile of NORRDDISKS='%s' failed, error %s, offset %d\n", 
						    ptn, errmsg, errofs);
		}
	}

	if (strstr(msg, " xfs ") || strstr(msg, " efs ") || strstr(msg, " cxfs ")) dsystype = DT_IRIX;
	else if (strstr(msg, "DASD")) dsystype = DT_AS400;
	else if (strstr(msg, "NetWare Volumes")) dsystype = DT_NETWARE;
	else if (strstr(msg, "NetAPP")) dsystype = DT_NETAPP;
	else if (strstr(msg, "netapp.pl")) dsystype = DT_NETAPP2;
	else if (strstr(msg, "dbcheck.pl")) dsystype = DT_DBCHECK;
	else if (strstr(msg, "Summary")) dsystype = DT_BBWIN; /* Make sure it is a bbwin client v > 0.10 */
	else if (strstr(msg, "Filesystem")) dsystype = DT_NT;
	else dsystype = DT_UNIX;

	curline = msg;

	switch (dsystype) {
	  case DT_NETAPP2:
		curline = strchr(curline, '\n'); if (curline) curline++;
		break;
	  case DT_DBCHECK:
		curline = strchr(curline, '\n'); if (curline) curline++;
		curline=strstr(curline, "TableSpace/DBSpace");
		if (curline) {
			eoln = strchr(curline, '\n');
			curline = (eoln ? (eoln+1) : NULL);
		}
		break;
	}

	while (curline)  {
		char *fsline, *p;
		char *columns[20];
		int columncount;
		char *diskname = NULL;
		int pused = -1;
		int wanteddisk = 1;
		long long aused = 0;
		/* FD: Using double instead of long long because we can have decimal on Netapp and DbCheck */
		double dused = 0;
		/* FD: used to add a column if the filesystem is named "snap reserve" for netapp.pl */
		int snapreserve=0;

		eoln = strchr(curline, '\n'); if (eoln) *eoln = '\0';

		/* AS/400 reports must contain the word DASD */
		if ((dsystype == DT_AS400) && (strstr(curline, "DASD") == NULL)) goto nextline;

		/* FD: Exit if doing DBCHECK and the end of the tablespaces are reached */
		if ((dsystype == DT_DBCHECK) && (strstr(eoln+1, "dbcheck.pl") == (eoln+1))) break;

		/* FD: netapp.pl snapshot line that start with "snap reserve" need a +1 */
		if ((dsystype == DT_NETAPP2) && (strstr(curline, "snap reserve"))) snapreserve=1;

		/* All clients except AS/400 and DBCHECK report the mount-point with slashes - ALSO Win32 clients. */
		if ((dsystype != DT_AS400) && (dsystype != DT_DBCHECK) && (strchr(curline, '/') == NULL)) goto nextline;

		/* red/yellow filesystems show up twice */
		if ((dsystype != DT_NETAPP) && (dsystype != DT_NETWARE) && (dsystype != DT_AS400)) {
			if (*curline == '&') goto nextline; 
			if ((strstr(curline, " red ") || strstr(curline, " yellow "))) goto nextline;
		}

		for (columncount=0; (columncount<20); columncount++) columns[columncount] = "";
		fsline = xstrdup(curline); columncount = 0; p = strtok(fsline, " ");
		while (p && (columncount < 20)) { columns[columncount++] = p; p = strtok(NULL, " "); }

		/* 
		 * Some Unix filesystem reports contain the word "Filesystem".
		 * So check if there's a slash in the NT filesystem letter - if yes,
		 * then it's really a Unix system after all.
		 * Not always has BBWIN > 0.10 not give the information also on mounted disk.
		 * (IE more than one letter)
		 */
		if ( (dsystype == DT_NT) && (*(columns[5])) &&
		     ((strchr(columns[0], '/')) || (strlen(columns[0]) > 1)) )
			dsystype = DT_UNIX;

		switch (dsystype) {
		  case DT_IRIX:
			diskname = xstrdup(columns[6]);
			p = strchr(columns[5], '%'); if (p) *p = ' ';
			pused = atoi(columns[5]);
			aused = atoi(columns[3]);
			break;
		  case DT_AS400:
			diskname = xstrdup("/DASD");
			p = strchr(columns[columncount-1], '%'); if (p) *p = ' ';
			/* 
			 * Yikes ... the format of this line varies depending on the color.
			 * Red:
			 *    March 23, 2005 12:32:54 PM EST DASD on deltacdc at panic level at 90.4967% 
			 * Yellow: 
			 *    April 4, 2005 9:20:26 AM EST DASD on deltacdc at warning level at 81.8919%
			 * Green:
			 *    April 3, 2005 7:53:53 PM EST DASD on deltacdc OK at 79.6986%
			 *
			 * So we'll just pick out the number from the last column.
			 */
			pused = atoi(columns[columncount-1]);
			aused = 0; /* Not available */
			break;
		  case DT_BBWIN:
		  case DT_NT:
			diskname = xmalloc(strlen(columns[0])+2);
			sprintf(diskname, "/%s", columns[0]);
			p = strchr(columns[4], '%'); if (p) *p = ' ';
			pused = atoi(columns[4]);
			aused = atoi(columns[2]);
			break;

		  /* FD: Check TableSpace from dbcheck.pl */
		  case DT_DBCHECK:
			/* FD: Add an initial "/" to TblSpace Name so they're reported in the trends column */
			diskname=xmalloc(strlen(columns[0])+2);
			sprintf(diskname,"/%s",columns[0]);
			p = strchr(columns[4], '%'); if (p) *p = ' ';
			pused = atoi(columns[4]);
			p = columns[2] + strspn(columns[2], "0123456789.");
			/* FD: Using double instead of long long because we can have decimal */
			dused = str2ll(columns[2], NULL);
			/* FD: dbspace report contains M/G/T 
			   Convert to KB if there's a modifier after the numbers
			*/
			if (*p == 'M') dused *= 1024;
			else if (*p == 'G') dused *= (1024*1024);
			else if (*p == 'T') dused *= (1024*1024*1024);
			aused=(long long)dused;
			break;
		  /* FD: Check diskspace from netapp.pl */
		  case DT_NETAPP2:
			/* FD: Name column can contain "spaces" so it could be split in multiple
			   columns, create a unique string from columns[5] that point to the
			   complete disk name 
			*/
			while (columncount-- > 6+snapreserve) { 
				p = strchr(columns[columncount-1],0); 
				if (p) *p = '_'; 
			}
			/* FD: Add an initial "/" to qtree and quotas */
			if (*columns[5+snapreserve] != '/') {
				diskname=xmalloc(strlen(columns[5+snapreserve])+2);
				sprintf(diskname,"/%s",columns[5+snapreserve]);
			} else {
				diskname = xstrdup(columns[5+snapreserve]);
			}
			p = strchr(columns[4+snapreserve], '%'); if (p) *p = ' ';
			pused = atoi(columns[4+snapreserve]);
			p = columns[2+snapreserve] + strspn(columns[2+snapreserve], "0123456789.");
			/* Using double instead of long long because we can have decimal */
			dused = str2ll(columns[2+snapreserve], NULL);
			/* snapshot and qtree contains M/G/T 
			   Convert to KB if there's a modifier after the numbers
			*/
			if (*p == 'M') dused *= 1024;
			else if (*p == 'G') dused *= (1024*1024);
			else if (*p == 'T') dused *= (1024*1024*1024);
			aused=(long long) dused;
			break;

		  case DT_UNIX:
			diskname = xstrdup(columns[5]);
			p = strchr(columns[4], '%'); if (p) *p = ' ';
			pused = atoi(columns[4]);
			aused = atoi(columns[2]);
			break;
		  case DT_NETAPP:
			diskname = xstrdup(columns[1]);
			pused = atoi(columns[5]);
			p = columns[3] + strspn(columns[3], "0123456789");
			aused = str2ll(columns[3], NULL);
			/* Convert to KB if there's a modifier after the numbers */
			if (*p == 'M') aused *= 1024;
			else if (*p == 'G') aused *= (1024*1024);
			else if (*p == 'T') aused *= (1024*1024*1024);
			break;
		  case DT_NETWARE:
			diskname = xstrdup(columns[1]);
			aused = str2ll(columns[3], NULL);
			pused = atoi(columns[7]);
			break;
		}

		/* Check include/exclude patterns */
		wanteddisk = 1;
		if (exclpattern) {
			int ovector[30];
			int result;

			result = pcre_exec(exclpattern, NULL, diskname, strlen(diskname), 
					   0, 0, ovector, (sizeof(ovector)/sizeof(int)));

			wanteddisk = (result < 0);
		}
		if (wanteddisk && inclpattern) {
			int ovector[30];
			int result;

			result = pcre_exec(inclpattern, NULL, diskname, strlen(diskname), 
					   0, 0, ovector, (sizeof(ovector)/sizeof(int)));

			wanteddisk = (result >= 0);
		}

		if (wanteddisk && diskname && (pused != -1)) {
			p = diskname; while ((p = strchr(p, '/')) != NULL) { *p = ','; }
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
			snprintf(rrdfn, sizeof(rrdfn)-1, "%s%s.rrd", testname, diskname);
			rrdfn[sizeof(rrdfn)-1] = '\0';
			sprintf(rrdvalues, "%d:%d:%lld", (int)tstamp, pused, aused);
			create_and_update_rrd(hostname, rrdfn, disk_params, disk_tpl);
		}
		if (diskname) { xfree(diskname); diskname = NULL; }

		if (eoln) *eoln = '\n';
		xfree(fsline);

nextline:
		curline = (eoln ? (eoln+1) : NULL);
	}

	return 0;
}

