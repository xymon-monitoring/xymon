/*----------------------------------------------------------------------------*/
/* Xymon RRD handler module.                                                  */
/*                                                                            */
/* Copyright (C) 2004-2011 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char do_net_rcsid[] = "$Id$";

int do_net_rrd(char *hostname, char *testname, char *classname, char *pagepaths, char *msg, time_t tstamp)
{
	static char *xymonnet_params[]       = { "DS:sec:GAUGE:600:0:U", NULL };
	static void *xymonnet_tpl            = NULL;

	char *p;
	float seconds = 0.0;
	int do_default = 1;

	if (xymonnet_tpl == NULL) xymonnet_tpl = setup_template(xymonnet_params);

	if (strcmp(testname, "http") == 0) {
		char *line1, *url = NULL, *eoln;

		do_default = 0;

		line1 = msg;
		while ((line1 = strchr(line1, '\n')) != NULL) {
			line1++; /* Skip the newline */
			eoln = strchr(line1, '\n'); if (eoln) *eoln = '\0';

			if ( (strncmp(line1, "&green", 6) == 0) || 
			     (strncmp(line1, "&yellow", 7) == 0) ||
			     (strncmp(line1, "&red", 4) == 0) ) {
				p = strstr(line1, "http");
				if (p) {
					url = xstrdup(p);
					p = strchr(url, ' ');
					if (p) *p = '\0';
				}
			}
			else if (url && ((p = strstr(line1, "Seconds:")) != NULL) && (sscanf(p, "Seconds: %f", &seconds) == 1)) {
				char *urlfn = url;

				if (strncmp(urlfn, "http://", 7) == 0) urlfn += 7;
				p = urlfn; while ((p = strchr(p, '/')) != NULL) *p = ',';
				setupfn3("%s.%s.%s.rrd", "tcp", "http", urlfn);
				snprintf(rrdvalues, sizeof(rrdvalues), "%d:%.2f", (int)tstamp, seconds);
				create_and_update_rrd(hostname, testname, classname, pagepaths, xymonnet_params, xymonnet_tpl);
				xfree(url); url = NULL;
			}

			if (eoln) *eoln = '\n';
		}

		if (url) xfree(url);
	}
	else if (strcmp(testname, xgetenv("PINGCOLUMN")) == 0) {
		/*
		 * Ping-tests, possibly using fping.
		 */
		char *tmod = "ms";

		do_default = 0;

		if ((p = strstr(msg, "time=")) != NULL) {
			/* Standard ping, reports ".... time=0.2 ms" */
			seconds = atof(p+5);
			tmod = p + 5; tmod += strspn(tmod, "0123456789. ");
		}
		else if ((p = strstr(msg, "alive")) != NULL) {
			/* fping, reports ".... alive (0.43 ms)" */
			seconds = atof(p+7);
			tmod = p + 7; tmod += strspn(tmod, "0123456789. ");
		}

		if (strncmp(tmod, "ms", 2) == 0) seconds = seconds / 1000.0;
		else if (strncmp(tmod, "usec", 4) == 0) seconds = seconds / 1000000.0;

		setupfn2("%s.%s.rrd", "tcp", testname);
		snprintf(rrdvalues, sizeof(rrdvalues), "%d:%.6f", (int)tstamp, seconds);
		return create_and_update_rrd(hostname, testname, classname, pagepaths, xymonnet_params, xymonnet_tpl);
	}
	else if (strcmp(testname, "ntp") == 0) {
		/*
		 * sntp output: 
		 *    2009 Nov 13 11:29:10.000313 + 0.038766 +/- 0.052900 secs
		 * ntpdate output: 
		 *    server 172.16.10.2, stratum 3, offset -0.040324, delay 0.02568
		 *    13 Nov 11:29:06 ntpdate[7038]: adjust time server 172.16.10.2 offset -0.040324 sec
		 */

		char dataforntpstat[100];
		char *offsetval = NULL;
		char offsetbuf[40];
		char *msgcopy = strdup(msg);

		if (strstr(msgcopy, "ntpdate") != NULL) {
			/* Old-style "ntpdate" output */
			char *p;

			p = strstr(msgcopy, "offset ");
			if (p) {
				p += 7;
				offsetval = strtok(p, " \r\n\t");
			}
		}
		else if (strstr(msgcopy, " secs") != NULL) {
			/* Probably new "sntp" output */
			char *year, *month, *tm, *offsetdirection, *offset, *plusminus, *errorbound, *secs;

			month = tm = offsetdirection = plusminus = errorbound = secs = NULL;
			year = strtok(msgcopy, " ");
			if (year) tm = strtok(NULL, " ");
			if (tm) offsetdirection = strtok(NULL, " ");
			if (offsetdirection) offset = strtok(NULL, " ");
			if (offset) plusminus = strtok(NULL, " ");
			if (plusminus) errorbound = strtok(NULL, " ");
			if (errorbound) secs = strtok(NULL, " ");

			if ( offsetdirection && ((strcmp(offsetdirection, "+") == 0) || (strcmp(offsetdirection, "-") == 0)) &&
			     plusminus && (strcmp(plusminus, "+/-") == 0) && 
			     secs && (strcmp(secs, "secs") == 0) ) {
				/* Looks sane */
				snprintf(offsetbuf, sizeof(offsetbuf), "%s%s", offsetdirection, offset);
				offsetval = offsetbuf;
			}
		}
		
		if (offsetval) {
			snprintf(dataforntpstat, sizeof(dataforntpstat), "offset=%s", offsetval);
			do_ntpstat_rrd(hostname, testname, classname, pagepaths, dataforntpstat, tstamp);
		}

		xfree(msgcopy);
	}


	if (do_default) {
		/*
		 * Normal network tests - pick up the "Seconds:" value
		 */
		p = strstr(msg, "\nSeconds:");
		if (p && (sscanf(p+1, "Seconds: %f", &seconds) == 1)) {
			setupfn2("%s.%s.rrd", "tcp", testname);
			snprintf(rrdvalues, sizeof(rrdvalues), "%d:%.2f", (int)tstamp, seconds);
			return create_and_update_rrd(hostname, testname, classname, pagepaths, xymonnet_params, xymonnet_tpl);
		}
	}

	return 0;
}

