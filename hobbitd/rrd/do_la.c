/*----------------------------------------------------------------------------*/
/* Hobbit RRD handler module.                                                 */
/*                                                                            */
/* Copyright (C) 2004 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char la_rcsid[] = "$Id: do_la.c,v 1.10 2005-02-17 21:53:52 henrik Exp $";

static char *la_params[]          = { "rrdcreate", rrdfn, "DS:la:GAUGE:600:0:U", rra1, rra2, rra3, rra4, NULL };

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

