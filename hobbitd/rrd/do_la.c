/*----------------------------------------------------------------------------*/
/* Hobbit RRD handler module.                                                 */
/*                                                                            */
/* Copyright (C) 2004-2005 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char la_rcsid[] = "$Id: do_la.c,v 1.13 2005-04-10 11:49:41 henrik Exp $";

static char *la_params[]          = { "rrdcreate", rrdfn, "DS:la:GAUGE:600:0:U", rra1, rra2, rra3, rra4, NULL };

static pcre *as400_exp = NULL;
static pcre *zVM_exp = NULL;

int do_la_larrd(char *hostname, char *testname, char *msg, time_t tstamp)
{
	char *p, *eoln;
	int gotusers=0, gotprocs=0, gotload=0;
	int users=0, procs=0, load=0;

	eoln = strchr(msg, '\n'); if (eoln) *eoln = '\0';
	p = strstr(msg, "up: ");
	if (!p) p = strstr(msg, "Uptime:");	/* Netapp filerstats2bb script */
	if (!p) p = strstr(msg, "uptime:");
	if (p) {
		/* First line of cpu report, contains "up: 159 days, 1 users, 169 procs, load=21" */
		p = strchr(p, ',');
		if (p) {
			gotusers = (sscanf(p, ", %d users", &users) == 1);
			p = strchr(p+1, ',');
		}

		if (p) {
			gotprocs = (sscanf(p, ", %d procs", &procs) == 1);
			p = strchr(p+1, ',');
		}

		/*
		 * Load can be either 
		 * -  "load=xx%"   (Windows)
		 * -  "load=xx.xx" (Unix, DISPREALLOADAVG=TRUE)
		 * -  "load=xx"    (Unix, DISPREALLOADAVG=FALSE)
		 *
		 * We want the load in percent (Windows), or LA*100 (Unix).
		 */
		p = strstr(msg, "load="); 
		if (p) { 
			p += 5;
			if (strchr(p, '%')) {
				gotload = 1; 
				load = atoi(p);
			}
			else if (strchr(p, '.')) {
				gotload = 1; 
				load = 100*atoi(p);
				/* Find the decimal part, and cut off at 2 decimals */
				p = strchr(p, '.'); if (strlen(p) > 3) *(p+3) = '\0';
				load += atoi(p+1);
			}
			else {
				gotload = 1; 
				load = atoi(p);
			}
		}
	}
	else if (strstr(msg, "z/VM")) {
		/* z/VM cpu message. Looks like this, from Rich Smrcina:
		 * green 5 Apr 2005 20:07:34  CPU Utilization  7% z/VM Version 4 Release 4.0, service level 0402 (32-bit) AVGPROC-007% 01
		 */

		int ovector[30];
		char w[100];
		int res;

		if (zVM_exp == NULL) {
			const char *errmsg = NULL;
			int errofs = 0;

			zVM_exp = pcre_compile(".* CPU Utilization *([0-9]+)%", PCRE_CASELESS, &errmsg, &errofs, NULL);
		}

		res = pcre_exec(zVM_exp, NULL, msg, strlen(msg), 0, 0, ovector, (sizeof(ovector)/sizeof(int)));
		if (res >= 0) {
			/* We have a match - pick up the data. */
			*w = '\0'; if (res > 0) pcre_copy_substring(msg, ovector, res, 1, w, sizeof(w));
			if (strlen(w)) {
				load = atoi(w); gotload = 1;
			}
		}
	}
	else {
		/* 
		 * No "uptime" in message - could be from an AS/400. They look like this:
		 * green March 21, 2005 12:33:24 PM EST deltacdc 108 users 45525 jobs(126 batch,0 waiting for message), load=26%
		 */
		int ovector[30];
		char w[100];
		int res;

		if (as400_exp == NULL) {
			const char *errmsg = NULL;
			int errofs = 0;

			as400_exp = pcre_compile(".* ([0-9]+) users ([0-9]+) jobs.* load=([0-9]+)\%", 
						 PCRE_CASELESS, &errmsg, &errofs, NULL);
		}

		res = pcre_exec(as400_exp, NULL, msg, strlen(msg), 0, 0, ovector, (sizeof(ovector)/sizeof(int)));
		if (res >= 0) {
			/* We have a match - pick up the AS/400 data. */
			*w = '\0'; if (res > 0) pcre_copy_substring(msg, ovector, res, 1, w, sizeof(w));
			if (strlen(w)) {
				users = atoi(w); gotusers = 1;
			}

			*w = '\0'; if (res > 0) pcre_copy_substring(msg, ovector, res, 3, w, sizeof(w));
			if (strlen(w)) {
				load = atoi(w); gotload = 1;
			}
		}
	}

	if (eoln) *eoln = '\n';

	if (!gotload) {
		/* See if it's a report from the ciscocpu.pl script. */
		p = strstr(msg, "<br>CPU 5 min average:");
		if (p) {
			/* It reports in % cpu utilization */
			p = strchr(p, ':');
			load = atoi(p+1);
			gotload = 1;
		}
	}

	if (gotload) {
		sprintf(rrdfn, "la.rrd");
		sprintf(rrdvalues, "%d:%d", (int)tstamp, load);
		create_and_update_rrd(hostname, rrdfn, la_params, update_params);
	}

	if (gotprocs) {
		sprintf(rrdfn, "procs.rrd");
		sprintf(rrdvalues, "%d:%d", (int)tstamp, procs);
		create_and_update_rrd(hostname, rrdfn, la_params, update_params);
	}

	if (gotusers) {
		sprintf(rrdfn, "users.rrd");
		sprintf(rrdvalues, "%d:%d", (int)tstamp, users);
		create_and_update_rrd(hostname, rrdfn, la_params, update_params);
	}

	if (memhosts_init && (rbtFind(memhosts, hostname) == rbtEnd(memhosts))) {
		/* Pick up memory statistics */
		int found, realuse, swapuse;
		unsigned long phystotal, physavail, pagetotal, pageavail;

		found = realuse = swapuse = 0;
		phystotal = physavail = pagetotal = pageavail = 0;

		p = strstr(msg, "Total Physical memory:");
		if (p) { found++; phystotal = atol(strchr(p, ':') + 1); }

		if (found == 1) {
			p = strstr(msg, "Available Physical memory:");
			if (p) { found++; physavail = atol(strchr(p, ':') + 1); }
		}

		if (found == 2) {
			p = strstr(msg, "Total PageFile size:"); 
			if (p) { found++; pagetotal = atol(strchr(p, ':') + 1); }
		}

		if (found == 2) {
			p = strstr(msg, "Available PageFile size:"); 
			if (p) { found++; pageavail = atol(strchr(p, ':') + 1); }
		}

		if (found == 4) {
			realuse = 100 - ((physavail * 100) / phystotal);
			swapuse = 100 - ((pageavail * 100) / pagetotal);
		}

		do_memory_larrd_update(tstamp, hostname, realuse, swapuse, -1);
	}

	return 0;
}

