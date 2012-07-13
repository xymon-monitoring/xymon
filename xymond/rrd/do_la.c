/*----------------------------------------------------------------------------*/
/* Xymon RRD handler module.                                                  */
/*                                                                            */
/* Copyright (C) 2004-2011 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char la_rcsid[] = "$Id$";

int do_la_rrd(char *hostname, char *testname, char *classname, char *pagepaths, char *msg, time_t tstamp)
{
	static char *la_params[]          = { "DS:la:GAUGE:600:0:U", NULL };
	static void *la_tpl               = NULL;
	static char *clock_params[]       = { "DS:la:GAUGE:600:U:U", NULL }; /* "la" is a misnomer, but to stay compatiable with existing RRD files */
	static void *clock_tpl            = NULL;

	static pcre *as400_exp = NULL;
	static pcre *zVM_exp = NULL;
	static time_t starttime = 0;

	char *p, *eoln = NULL;
	int gotusers=0, gotprocs=0, gotload=0, gotclock=0;
	int users=0, procs=0, load=0, clockdiff=0;
	time_t now = getcurrenttime(NULL);

	if (la_tpl == NULL) la_tpl = setup_template(la_params);
	if (clock_tpl == NULL) clock_tpl = setup_template(clock_params);
	if (starttime == 0) starttime = now;

	if (strstr(msg, "bb-xsnmp")) {
		/*
		 * bb-xsnmp.pl script output.
		 *
		 * green Tue Apr  5 12:57:37 2005 up: 254.58 days, CPU Usage=  9%
		 *
		 * &green  CPU Time in Busy Mode:   9%
		 * &green  CPU Time in Idle Mode:  91%
		 *
		 * &yellow CPU Usage Threshold: 90%
		 * &red CPU Usage Threshold: 95%
		 *
		 * <!-- Enterprise: netapp , Version: 6.42 -->
		 * bb-xsnmp.pl Version: 1.78
		 */

		p = strstr(msg, "CPU Usage=");
		if (p) {
			p += strlen("CPU Usage=");
			gotload = 1;
			load = atoi(p);
		}

		goto done_parsing;
	}
	else if (strstr(msg, "z/VM") || strstr(msg, "VSE/ESA") || strstr(msg, "z/VSE") || strstr(msg, "z/OS")) {
		/* z/VM cpu message. Looks like this, from Rich Smrcina (client config mode):
		 * green 5 Apr 2005 20:07:34  CPU Utilization  7% z/VM Version 4 Release 4.0, service level 0402 (32-bit) AVGPROC-007% 01
		 * VSE/ESA or z/VSE cpu message.
		 * VSE/ESA 2.7.2 cr IPLed on ...
		 * or
		 * z/VSE 3.1.1 cr IPLed on ...
		 * In server (centralized) config mode or for z/OS (which is centralized config only)
		 * the operating system name is part of the message (as in the tests above).
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

		goto done_parsing;
	}

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

			as400_exp = pcre_compile(".* ([0-9]+) users ([0-9]+) jobs.* load=([0-9]+)\\%", 
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

done_parsing:
	if (eoln) *eoln = '\n';

	p = strstr(msg, "System clock is ");
	if (p) {
		if (sscanf(p, "System clock is %d seconds off", &clockdiff) == 1) gotclock = 1;
	}

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
		setupfn("%s.rrd", "la");
		snprintf(rrdvalues, sizeof(rrdvalues), "%d:%d", (int)tstamp, load);
		create_and_update_rrd(hostname, testname, classname, pagepaths, la_params, la_tpl);
	}

	if (gotprocs) {
		setupfn("%s.rrd", "procs");
		snprintf(rrdvalues, sizeof(rrdvalues), "%d:%d", (int)tstamp, procs);
		create_and_update_rrd(hostname, testname, classname, pagepaths, la_params, la_tpl);
	}

	if (gotusers) {
		setupfn("%s.rrd", "users");
		snprintf(rrdvalues, sizeof(rrdvalues), "%d:%d", (int)tstamp, users);
		create_and_update_rrd(hostname, testname, classname, pagepaths, la_params, la_tpl);
	}

	if (gotclock) {
		setupfn("%s.rrd", "clock");
		snprintf(rrdvalues, sizeof(rrdvalues), "%d:%d", (int)tstamp, clockdiff);
		create_and_update_rrd(hostname, testname, classname, pagepaths, clock_params, clock_tpl);
	}

	/*
	 * If we have run for less than 6 minutes, drop the memory updates here.
	 * We want to be sure not to use memory statistics from the CPU report
	 * if there is a memory add-on sending a separate memory statistics
	 */
	if ((now - starttime) < 360) return 0;

	if (!memhosts_init || (xtreeFind(memhosts, hostname) == xtreeEnd(memhosts))) {
		/* Pick up memory statistics */
		int found, overflow, realuse, swapuse;
		long long phystotal, physavail, pagetotal, pageavail;

		found = overflow = realuse = swapuse = 0;
		phystotal = physavail = pagetotal = pageavail = 0;

		p = strstr(msg, "Total Physical memory:");
		if (p) { 
			phystotal = str2ll(strchr(p, ':') + 1, NULL); 
			if (phystotal != LONG_MAX) found++; else overflow++;
		}

		if (found == 1) {
			p = strstr(msg, "Available Physical memory:");
			if (p) { 
				physavail = str2ll(strchr(p, ':') + 1, NULL); 
				if (physavail != LONG_MAX) found++; else overflow++;
			}
		}

		if (found == 2) {
			p = strstr(msg, "Total PageFile size:"); 
			if (p) { 
				pagetotal = str2ll(strchr(p, ':') + 1, NULL); 
				if (pagetotal != LONG_MAX) found++; else overflow++;
			}
		}

		if (found == 3) {
			p = strstr(msg, "Available PageFile size:"); 
			if (p) { 
				pageavail = str2ll(strchr(p, ':') + 1, NULL); 
				if (pageavail != LONG_MAX) found++; else overflow++;
			}
		}

		if (found == 4) {
			phystotal = phystotal / 100;
			pagetotal = pagetotal / 100;
			realuse = 100 - (physavail / phystotal);
			swapuse = 100 - (pageavail / pagetotal);
			do_memory_rrd_update(tstamp, hostname, testname, classname, pagepaths, realuse, swapuse, -1);
		}
		else if (overflow) {
			errprintf("Host %s cpu report overflows in memory usage calculation\n", hostname);
		}
	}

	return 0;
}

